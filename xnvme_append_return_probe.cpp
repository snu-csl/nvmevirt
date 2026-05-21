#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>

#include <libxnvme.h>
#include <libxnvme_znd.h>

namespace {

[[nodiscard]] uint64_t parse_u64(std::string_view s, const char *name) {
    if (s.empty())
        throw std::invalid_argument(std::string(name) + " is empty");

    errno = 0;
    char *end = nullptr;
    const auto *ptr = s.data();
    const auto value = std::strtoull(ptr, &end, 0);

    if (errno != 0 || end != ptr + s.size())
        throw std::invalid_argument(std::string("invalid ") + name + ": " + std::string(s));

    return value;
}

void usage(const char *prog) {
    std::println(stderr,
                 "Usage: {} /dev/nvmeXnY <zone_slba> [append_count] [page_bytes]\n"
                 "\n"
                 "Example:\n"
                 "  sudo {} /dev/nvme1n1 0x1e50000 8 4096\n"
                 "\n"
                 "Args:\n"
                 "  /dev/nvmeXnY  ZNS device path\n"
                 "  zone_slba      Target zone start LBA (hex or dec)\n"
                 "  append_count   Number of append commands (default: 8)\n"
                 "  page_bytes     Bytes per append (default: 4096)\n",
                 prog, prog);
}

[[nodiscard]] uint64_t read_zone_wp(struct xnvme_dev *dev, uint64_t zone_slba) {
    struct xnvme_spec_znd_descr zdescr{};
    const int err = xnvme_znd_descr_from_dev(dev, zone_slba, &zdescr);
    if (err)
        throw std::runtime_error("xnvme_znd_descr_from_dev failed");
    return zdescr.wp;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3 || std::string_view(argv[1]) == "-h" || std::string_view(argv[1]) == "--help") {
        usage(argv[0]);
        return argc < 3 ? 1 : 0;
    }

    const char *dev_path = argv[1];
    const uint64_t zone_slba = parse_u64(argv[2], "zone_slba");
    const uint64_t append_count = argc >= 4 ? parse_u64(argv[3], "append_count") : 8;
    const uint64_t page_bytes = argc >= 5 ? parse_u64(argv[4], "page_bytes") : 4096;

    if (append_count == 0) {
        std::println(stderr, "append_count must be > 0");
        return 1;
    }

    struct xnvme_dev *dev = xnvme_dev_open(dev_path, nullptr);
    if (!dev) {
        std::println(stderr, "xnvme_dev_open({}) failed", dev_path);
        return 1;
    }

    int exit_code = 0;
    try {
        const auto *geo = xnvme_dev_get_geo(dev);
        const uint32_t nsid = xnvme_dev_get_nsid(dev);
        const uint64_t sectsz = geo->lba_nbytes;

        if (page_bytes == 0 || page_bytes % sectsz != 0)
            throw std::runtime_error("page_bytes must be a positive multiple of device sector size");

        const uint64_t sects_per_append = page_bytes / sectsz;
        if (sects_per_append == 0 || sects_per_append > UINT16_MAX + 1ull)
            throw std::runtime_error("invalid sects_per_append; choose a smaller page_bytes");

        std::println("[probe] dev={} nsid={} sectsz={} zone_slba={:#x} append_count={} page_bytes={} sects_per_append={}",
                     dev_path, nsid, sectsz, zone_slba, append_count, page_bytes, sects_per_append);

        {
            struct xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
            int err = xnvme_znd_mgmt_send(&ctx, nsid, zone_slba, false,
                                          XNVME_SPEC_ZND_CMD_MGMT_SEND_RESET,
                                          (enum xnvme_spec_znd_mgmt_send_action_so)0,
                                          nullptr);
            if (err || xnvme_cmd_ctx_cpl_status(&ctx))
                throw std::runtime_error("zone reset failed before probe");
        }

        void *buf = xnvme_buf_alloc(dev, page_bytes);
        if (!buf)
            throw std::runtime_error("xnvme_buf_alloc failed");

        std::memset(buf, 0xA5, page_bytes);

        uint64_t zero_cdw0 = 0;
        uint64_t mismatch = 0;
        uint64_t wp_mismatch = 0;
        uint64_t wp_before = read_zone_wp(dev, zone_slba);

        std::println("[probe] initial_wp={:#x}", wp_before);

        for (uint64_t i = 0; i < append_count; i++) {
            struct xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
            const int err = xnvme_znd_append(&ctx, nsid, zone_slba,
                                             (uint16_t)(sects_per_append - 1),
                                             buf, nullptr);
            const uint16_t cpl = xnvme_cmd_ctx_cpl_status(&ctx);

            if (err || cpl) {
                std::println(stderr,
                             "[probe] append#{} failed: err={} cpl_status={:#x}",
                             i, err, cpl);
                exit_code = 2;
                break;
            }

            const uint64_t reported = ctx.cpl.result;
            const uint64_t expected = zone_slba + i * sects_per_append;
            const bool is_match = (reported == expected);
            const uint64_t wp_after = read_zone_wp(dev, zone_slba);
            const uint64_t expected_wp_after = wp_before + sects_per_append;
            const bool wp_ok = (wp_after == expected_wp_after);

            if (reported == 0)
                zero_cdw0++;
            if (!is_match)
                mismatch++;
            if (!wp_ok)
                wp_mismatch++;

            std::println("[probe] append#{:<3} reported={:#x} expected={:#x} {} | wp_before={:#x} wp_after={:#x} expected_wp_after={:#x} {}",
                         i,
                         reported,
                         expected,
                         is_match ? "OK" : "MISMATCH",
                         wp_before,
                         wp_after,
                         expected_wp_after,
                         wp_ok ? "WP_OK" : "WP_MISMATCH");

            wp_before = wp_after;
        }

        xnvme_buf_free(dev, buf);

        std::println("\n[probe] summary: zero_cdw0={} completion_mismatch={} wp_mismatch={} total={}",
                     zero_cdw0, mismatch, wp_mismatch, append_count);

        if (zero_cdw0 > 0)
            std::println("[probe] observation: completion cdw0 returns 0 on successful append in this stack.");
        if (mismatch > 0 && wp_mismatch == 0)
            std::println("[probe] attribution: write-pointer advances correctly; mismatch is isolated to append completion return path.");
    } catch (const std::exception &e) {
        std::println(stderr, "[probe] error: {}", e.what());
        exit_code = 1;
    }

    xnvme_dev_close(dev);
    return exit_code;
}

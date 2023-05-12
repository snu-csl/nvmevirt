// SPDX-License-Identifier: GPL-2.0-only

#ifndef _NVME_ZNS_H
#define _NVME_ZNS_H

#include "nvme.h"

/* admin command */
struct nvme_id_zns_ctrl {
	__u8 zasl; // zone append size limit
	__u8 rsvd1[4095];
};

struct nvme_zns_lbaf {
	__le64 zsze;
	__u8 zdes;
	__u8 resv[7];
};

struct nvme_id_zns_ns {
	__u16 zoc; // zone operation characteristics
	__u16 ozcs; // optional zoned command support
		// (read across zone boundaries, ZRWA)
	__le32 mar; // maximum active resources
	__le32 mor; // maximum open resources
	__le32 rrl;
	__le32 frl;
	__le32 rsv1[6];
	__le32 numzrwa; // Maximum ZRWA Resources
	__le16 zrwafg; // zrwa flush granularity (lbas)
	__le16 zrwasz; // zrwa size (lbas)
	__u8 zrwacap; // zrwa capability (bit 0: explicit zrwa flush)
	__u8 rsv2[2763];
	struct nvme_zns_lbaf lbaf[16];
	__u8 rsv3[768];
	__u8 vs[256];
};

enum {
	NVME_SC_ZNS_INVALID_ZONE_OPERATION = ((NVME_SCT_CMD_SPECIFIC_STATUS << 8) | 0xB6),
	NVME_SC_ZNS_ZRWA_RSRC_UNAVAIL,
	NVME_SC_ZNS_ERR_BOUNDARY,
	NVME_SC_ZNS_ERR_FULL,
	NVME_SC_ZNS_ERR_READ_ONLY,
	NVME_SC_ZNS_ERR_OFFLINE,
	NVME_SC_ZNS_INVALID_WRITE,
	NVME_SC_ZNS_NO_ACTIVE_ZONE,
	NVME_SC_ZNS_NO_OPEN_ZONE,
	NVME_SC_ZNS_INVALID_TRANSITION
};

enum {
	OZCS_READ_ACROSS_ZONE = (1 << 0),
	OZCS_ZRWA = (1 << 1),
};

enum {
	ZRWACAP_EXPFLUSHSUP = (1 << 0),
};

enum { ZONE_TYPE_SEQ_WRITE_REQUIRED = 0x2 };

enum zone_state {
	ZONE_STATE_EMPTY = 0x1,
	ZONE_STATE_OPENED_IMPL = 0x2,
	ZONE_STATE_OPENED_EXPL = 0x3,
	ZONE_STATE_CLOSED = 0x4,
	ZONE_STATE_READ_ONLY = 0xD,
	ZONE_STATE_FULL = 0xE,
	ZONE_STATE_OFFLINE = 0xF
};

enum zone_resource_type { ACTIVE_ZONE, OPEN_ZONE, ZRWA_ZONE, RES_TYPE_COUNT };

struct zone_descriptor {
	__u8 type : 4;
	__u8 : 4;
	__u8 : 4;
	__u8 state : 4;

	union {
		__u8 za; // zone attributes

		struct {
			__u8 zfc : 1; // zone finished by controller
			__u8 fzr : 1; // finish zone recommended
			__u8 rzr : 1; // reset zone recommended
			__u8 zrwav : 1; //zone random write area valid
			__u8 : 3;
			__u8 zdef : 1; // zone descriptor extension valid
		};
	};

	union {
		__u8 zai; // zone attributes information
		struct {
			__u8 fzrtl : 2; // finish zone recommnded time limit
			__u8 rzrtl : 2; // reset zone recommended time limit
			__u8 : 4;
		};
	};

	__u32 reserved;
	__u64 zone_capacity;
	__u64 zslba; // zone start logical block address
	__u64 wp;

	__u32 rsvd[8];
};

struct zone_report {
	__u64 nr_zones;
	__u64 rsvd[7];

	//several zone descriptors..
	struct zone_descriptor zd[1];
};

// zone management receive command
struct nvme_zone_mgmt_recv {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__le32 cdw2[2];
	__le64 metadata;
	__le64 prp1;
	__le64 prp2;
	__le64 slba; // DW 10, 11
	__le32 nr_dw; // DW 12

	union {
		__le32 DW13;
		struct {
			__u8 zra; // zone recive action 00h : report zones, 01h : extended report zones
			__u8 zra_specific_field;
			__u16 zra_specific_features : 1;
			__u16 reserved : 15;
		};
	};

	__le32 rsvd[3];
};

enum zone_send_action {
	ZSA_CLOSE_ZONE = 0x1,
	ZSA_FINISH_ZONE,
	ZSA_OPEN_ZONE,
	ZSA_RESET_ZONE,
	ZSA_OFFLINE_ZONE,
	ZSA_SET_ZONE_DESC_EXT = 0x10,
	ZSA_FLUSH_EXPL_ZRWA = 0x11,
};

// zone management send command
struct nvme_zone_mgmt_send {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__le32 cdw2[2];
	__le64 metadata;
	__le64 prp1;
	__le64 prp2;
	__le64 slba; // DW 10, 11
	__le32 reserved; // DW 12 Not used.

	union {
		__le32 DW13;
		struct {
			__u32 zsa : 8; // zone send action
				// 01h : close, 02h : finish
				// 03h : open, 04h: reset
				// 05h:offline, 10h:set zone descriptor extension
			__u32 select_all : 1; // 1 : SLBA field is ignored,
			__u32 zsaso : 1; // Zone send action specific option
			__u32 : 22;
		};
	};

	__le32 rsvd[3];
};

struct nvme_zone_append {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__le32 cdw2[2];
	__le64 metadata;
	__le64 prp1;
	__le64 prp2;
	__le64 slba; // DW 10, 11
	union {
		__le32 DW12;
		struct {
			__le32 nr_lba : 16;
			__le32 : 4;
			__le32 dtype : 4; // directive type
			__le32 stc : 1; // storage tag check
			__le32 piremap : 1; //protection information remap
			__le32 printfo : 4; //protection information field
			__le32 fua : 1; ///force unit access
			__le32 lr : 1; //limited retry
		};
	};

	union {
		__le32 DW13;
		struct {
			__le32 : 16;
			__le32 dspec : 16; // directive specific
		};
	};

	__le32 DW14;

	union {
		__le32 DW15;
		struct {
			__le32 lbat : 16; //logical block application tag
			__le32 lbatm : 16; // logical block application tag mask
		};
	};
};
#endif

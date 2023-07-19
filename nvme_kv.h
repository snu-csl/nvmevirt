// SPDX-License-Identifier: GPL-2.0-only

#ifndef _NVME_KV_H
#define _NVME_KV_H

#include "nvme.h"

#define KVCMD_INLINE_KEY_MAX (16)
#define KVCMD_MAX_KEY_SIZE (255)
#define KVCMD_MIN_KEY_SIZE (4)
#define MAX_SUB_CMD (8)
#define ALIGN_LEN (64)

union nvme_data_ptr {
	struct {
		__le64 prp1;
		__le64 prp2;
	};
};

/*KV-SSD Command*/
struct nvme_kv_store_command {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__u64 rsvd;
	__le32 offset;
	__u32 rsvd2;
	union nvme_data_ptr dptr; /* value dptr prp1,2 */
	__le32 value_len; /* size in word */
	__u8 key_len; /* 0 ~ 255 (keylen - 1) */
	__u8 option;
	__u8 invalid_byte : 2;
	__u8 rsvd3 : 6;
	__u8 rsvd4;
	union {
		struct {
			char key[16];
		};
		struct {
			__le64 key_prp;
			__le64 key_prp2;
		};
	};
};

struct nvme_kv_append_command {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__u64 rsvd;
	__le32 offset;
	__u32 rsvd2;
	union nvme_data_ptr dptr; /* value dptr prp1,2 */
	__le32 value_len; /* size in word */
	__u8 key_len; /* 0 ~ 255 (keylen - 1) */
	__u8 option;
	__u8 invalid_byte : 2;
	__u8 rsvd3 : 6;
	__u8 rsvd4;
	union {
		struct {
			char key[16];
		};
		struct {
			__le64 key_prp;
			__le64 key_prp2;
		};
	};
};

struct nvme_kv_batch_command {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__u64 rsvd;
	__le32 offset;
	__u32 rsvd2;
	union nvme_data_ptr dptr; /* value dptr prp1,2 */
	__le32 value_len; /* size in word */
	__u8 key_len; /* 0 ~ 255 (key len -1) */
	__u8 option;
	__u8 rsvd3;
	__u8 rsvd4;
	union {
		struct {
			char key[16];
		};
		struct {
			__le64 key_prp;
			__le64 key_prp2;
		};
	};
};

struct nvme_kv_retrieve_command {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__u64 rsvd;
	__le32 offset;
	__u32 rsvd2;
	union nvme_data_ptr dptr; /* value dptr prp1,2 */
	__le32 value_len; /* size in word */
	__u8 key_len; /* 0 ~ 255 (keylen - 1) */
	__u8 option;
	__u16 rsvd3;
	union {
		struct {
			char key[16];
		};
		struct {
			__le64 key_prp;
			__le64 key_prp2;
		};
	};
};

struct nvme_kv_delete_command {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__u64 rsvd;
	__le32 offset;
	__u32 rsvd2;
	__u64 rsvd3[2];
	__le32 value_len; /* size in word */
	__u8 key_len; /* 0 ~ 255 (keylen - 1) */
	__u8 option;
	__u16 rsvd4;
	union {
		struct {
			char key[16];
		};
		struct {
			__le64 key_prp;
			__le64 key_prp2;
		};
	};
};

struct nvme_kv_iter_req_command {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__u64 rsvd[4];
	__le32 zero; /* should be zero */
	__u8 iter_handle;
	__u8 option;
	__u16 rsvd2;
	__u32 iter_val;
	__u32 iter_bitmask;
	__u64 rsvd3;
};

struct nvme_kv_iter_read_command {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__u64 rsvd[2];
	union nvme_data_ptr dptr; /* value dptr prp1,2 */
	__le32 value_len; /* size in word */
	__u8 iter_handle;
	__u8 option;
	__u16 rsvd2;
	__u64 rsvd3[2];
};

struct nvme_kv_exist_command {
	__u8 opcode;
	__u8 flags;
	__u16 command_id;
	__le32 nsid;
	__u64 rsvd;
	__le32 offset;
	__u32 rsvd2;
	__u64 rsvd3[2];
	__le32 value_len; /* size in word */
	__u8 key_len; /* 0 ~ 255 (keylen - 1) */
	__u8 option;
	__u16 rsvd4;
	union {
		struct {
			char key[16];
		};
		struct {
			__le64 key_prp;
			__le64 key_prp2;
		};
	};
};

/* Additional structures for iterator */
struct kv_iter_context {
	unsigned char handle;
	unsigned int prefix;
	unsigned int bitmask;
	void *buf;
	int buflen;
	int byteswritten;
	int bufoffset;
	bool end;
	unsigned long current_pos;
};

enum nvme_kv_iter_req_option {
	ITER_OPTION_NOTHING = 0x0,
	ITER_OPTION_OPEN = 0x01,
	ITER_OPTION_CLOSE = 0x02,
	ITER_OPTION_KEY_ONLY = 0x04,
	ITER_OPTION_KEY_VALUE = 0x08,
	ITER_OPTION_DEL_KEY_VALUE = 0x10,
};

/* Additional structures for batch */
struct sub_cmd_attribute {
	__u8 opcode; // DW0  support only 0x81 and 0xA0
	__u8 keySize; // DW0 [15:08] Keys size
	__u8 reservedDw0[2]; // DW0 [31:16] Reserved
	__u32 valueSize; // DW1  Value size
	__u8 option; // DW2 [07:00] Option. Follow the command option definition corresponding to the sub command opcode.
	__u8 nsid; // DW2 [15:08] Key space ID
	__u8 reservedDw2[2]; // DW2 [31:16] Reserved
	__u32 NoUsed; // DW3
};

struct batch_cmd_head {
	struct sub_cmd_attribute attr[MAX_SUB_CMD];
	__u8 reserved[384];
};

struct sub_cmd_head {
	__u8 reserved[ALIGN_LEN];
};

struct sub_payload_format {
	struct sub_cmd_head sub_head;
	__u8 data[0];
};

struct payload_format {
	struct batch_cmd_head batch_head;
	__u8 sub_payload[0];
};

struct sub_payload_attribute {
	__u32 sub_payload_len;
	__u32 key_len;
	__u32 value_len;
};

struct kv_batch_data {
	__u8 key[256];
	__u32 keySize;
	__u8 value[4097];
	__u32 valueSize;
	__u32 opcode;
};

struct nvme_kv_command {
	union {
		struct nvme_common_command common;
		struct nvme_kv_store_command kv_store;
		struct nvme_kv_retrieve_command kv_retrieve;
		struct nvme_kv_delete_command kv_delete;
		struct nvme_kv_append_command kv_append;
		struct nvme_kv_iter_req_command kv_iter_req;
		struct nvme_kv_iter_read_command kv_iter_read;
		struct nvme_kv_exist_command kv_exist;
		struct nvme_kv_batch_command kv_batch;
	};
};
#endif

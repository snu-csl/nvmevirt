// SPDX-License-Identifier: GPL-2.0-only

#ifndef _LIB_NVMEV_HDR_H
#define _LIB_NVMEV_HDR_H

struct __nvme_bar {
	uint64_t cap; /* Controller Capabilities */
	uint32_t vs; /* Version */
	uint32_t intms; /* Interrupt Mask Set */
	uint32_t intmc; /* Interrupt Mask Clear */
	uint32_t cc; /* Controller Configuration */
	uint32_t rsvd1; /* Reserved */
	uint32_t csts; /* Controller Status */
	uint32_t nssr; /* Subsystem Reset */
	uint32_t aqa; /* Admin Queue Attributes */
	uint64_t asq; /* Admin SQ Base Address */
	uint64_t acq; /* Admin CQ Base Address */
	uint32_t cmbloc; /* Controller Memory Buffer Location */
	uint32_t cmbsz; /* Controller Memory Buffer Size */
};

struct pci_header {
	struct {
		u16 vid;
		u16 did;
	} id;
	struct {
		u8 iose : 1;
		u8 mse : 1;
		u8 bme : 1;
		u8 sce : 1;
		u8 mwie : 1;
		u8 vga : 1;
		u8 pee : 1;
		u8 fixed : 1;
		u8 see : 1;
		u8 fbe : 1;
		u8 id : 1;
		u8 rsvd : 5;
	} cmd;
	struct {
		u8 rsvd1 : 3;
		u8 is : 1;
		u8 cl : 1;
		u8 c66 : 1;
		u8 rsvd2 : 1;
		u8 fbc : 1;
		u8 dpd : 1;
		u8 devt : 2;
		u8 sta : 1;
		u8 rta : 1;
		u8 rma : 1;
		u8 sse : 1;
		u8 dpe : 1;
	} sts;
	u8 rid;
	struct {
		u8 pi;
		u8 scc;
		u8 bcc;
	} cc;
	u8 cls;
	u8 mlt;
	struct {
		u8 hl : 7;
		u8 mfd : 1;
	} htype;
	struct {
		u8 cc : 4;
		u8 rsvd : 2;
		u8 sb : 1;
		u8 bc : 1;
	} bist;

	struct {
		u32 rte : 1;
		u32 tp : 2;
		u32 pf : 1;
		u32 rsvd : 10;
		u32 ba : 18;
	} mlbar;

	u32 mulbar;
	u32 idbar;

	u32 bar3;
	u32 bar4;
	u32 bar5;

	u32 ccptr;

	struct {
		u16 ssvid;
		u16 ssid;
	} ss;

	u32 erom;
	u8 cap;
	u8 rsvd[7];
	struct {
		u8 iline;
		u8 ipin;
	} intr;

	u8 mgnt;
	u8 mlat;
};

struct pci_pm_cap {
	struct {
		u8 cid;
		u8 next;
	} pid;
	struct {
		u16 vs : 3;
		u16 pmec : 1;
		u16 resv : 1;
		u16 dsi : 1;
		u16 auxc : 3;
		u16 d1s : 1;
		u16 d2s : 1;
		u16 psup : 5;
	} pc;
	struct {
		u16 ps : 2;
		u16 rsvd01 : 1;
		u16 nsfrst : 1;
		u16 rsvd02 : 4;
		u16 pmee : 1;
		u16 dse : 4;
		u16 dsc : 2;
		u16 pmes : 1;
	} pmcs;
	u8 ext[2];
};

struct pci_msi_cap {
	struct {
	} mid;
	struct {
	} mc;
	struct {
	} ma;
	struct {
	} mua;
	struct {
	} md;
	struct {
	} mmask;
	struct {
	} mpend;
};

struct pci_msix_cap {
	struct {
		u8 cid;
		u8 next;
	} mxid;
	struct {
		u16 ts : 11;
		u16 rsvd : 3;
		u16 fm : 1;
		u16 mxe : 1;
	} mxc;
	struct {
		u32 tbir : 3;
		u32 to : 29;
	} mtab;
	struct {
		u32 pbir : 3;
		u32 pbao : 29;
	} mpba;
};

struct pcie_cap {
	struct {
		u8 cid;
		u8 next;
	} pxid;
	struct {
		u8 ver : 4;
		u8 dpt : 4;
		u8 si : 1;
		u8 imn : 5;
		u8 rsvd : 2;
	} pxcap;
	struct {
		u32 mps : 3;
		u32 pfs : 2;
		u32 etfs : 1;
		u32 l0sl : 3;
		u32 l1l : 3;
		u32 rsvd : 3;
		u32 rer : 1;
		u32 rsvd2 : 2;
		u32 csplv : 8;
		u32 cspls : 2;
		u32 flrc : 1;
		u32 rsvd3 : 3;
	} pxdcap;
	struct {
		u16 cere : 1;
		u16 nfere : 1;
		u16 fere : 2;
		u16 urre : 1;
		u16 ero : 1;
		u16 mps : 3;
		u16 ete : 1;
		u16 pfe : 1;
		u16 appme : 1;
		u16 ens : 1;
		u16 mrrs : 3;
		u16 iflr : 1;
	} pxdc;
	struct {
		u16 ced : 1;
		u16 nfed : 1;
		u16 fed : 1;
		u16 urd : 1;
		u16 apd : 1;
		u16 tp : 1;
		u16 rsvd : 10;
	} pxds;
	struct {
		u32 sls : 4;
		u32 mlw : 6;
		u32 aspms : 2;
		u32 l0sel : 3;
		u32 l1el : 3;
		u32 cpm : 1;
		u32 sderc : 1;
		u32 dllla : 20;
		u32 lbnc : 1;
		u32 aoc : 1;
		u32 rsvd : 1;
		u32 pn : 8;
	} pxlcap;
	struct {
		u16 aspmc : 2;
		u16 rsvd : 1;
		u16 rcb : 1;
		u16 rsvd2 : 2;
		u16 ccc : 1;
		u16 es : 1;
		u16 ecpm : 1;
		u16 hawd : 1;
		u16 rsvd3 : 6;
	} pxlc;
	struct {
		u16 clc : 4;
		u16 nlw : 6;
		u16 rsvd : 2;
		u16 scc : 1;
		u16 rsvd2 : 3;
	} pxls;
	struct {
		u32 ctrs : 4;
		u32 ctds : 1;
		u32 arifs : 1;
		u32 aors : 1;
		u32 aocs32 : 1;
		u32 aocs : 1;
		u32 ccs128 : 1;
		u32 nprpr : 1;
		u32 ltrs : 1;
		u32 tphcs : 2;
		u32 rsvd : 4;
		u32 obffs : 2;
		u32 effs : 1;
		u32 eetps : 1;
		u32 meetp : 2;
		u32 rsvd2 : 8;
	} pxdcap2;
	struct {
		u32 ctv : 4;
		u32 ctd : 1;
		u32 rsvd : 5;
		u32 ltrme : 1;
		u32 rsvd2 : 2;
		u32 obffe : 2;
		u32 rsvd3 : 17;
	} pxdc2;
};

struct pci_ext_cap {
	u16 cid;
	u16 cver : 4;
	u16 next : 12;
};

struct pci_ext_cap_aer {
	struct pci_ext_cap id;
	struct {
		u32 rsvd : 4;
		u32 dlpes : 1;
		u32 rsvd2 : 7;
		u32 pts : 1;
		u32 fcpes : 1;
		u32 cts : 1;
		u32 cas : 1;
		u32 ucs : 1;
		u32 ros : 1;
		u32 mts : 1;
		u32 ecrces : 1;
		u32 ures : 1;
		u32 acsvs : 1;
		u32 uies : 1;
		u32 mcbts : 1;
		u32 aoebs : 1;
		u32 tpbes : 1;
		u32 rsvd3 : 6;
	} aeruces;
	struct {
		u32 rsvd : 4;
		u32 dlpem : 1;
		u32 rsvd2 : 7;
		u32 ptm : 1;
		u32 fcpem : 1;
		u32 ctm : 1;
		u32 cam : 1;
		u32 ucm : 1;
		u32 rom : 1;
		u32 mtm : 1;
		u32 ecrcem : 1;
		u32 urem : 1;
		u32 acsvm : 1;
		u32 uiem : 1;
		u32 mcbtm : 1;
		u32 aoebm : 1;
		u32 tpbem : 1;
		u32 rsvd3 : 6;
	} aerucem;
	struct {
		u32 rsvd : 4;
		u32 dlpesev : 1;
		u32 rsvd2 : 7;
		u32 ptsev : 1;
		u32 fcpesev : 1;
		u32 ctsev : 1;
		u32 casev : 1;
		u32 ucsev : 1;
		u32 rosev : 1;
		u32 mtsev : 1;
		u32 ecrcesev : 1;
		u32 uresev : 1;
		u32 acsvsev : 1;
		u32 uiesev : 1;
		u32 mcbtsev : 1;
		u32 aoebsev : 1;
		u32 tpbesev : 1;
		u32 rsvd3 : 6;
	} aerucesev;
	struct {
		u32 res : 1;
		u32 rsvd : 5;
		u32 bts : 1;
		u32 bds : 1;
		u32 rrs : 1;
		u32 rsvd2 : 3;
		u32 rts : 1;
		u32 anfes : 1;
		u32 cies : 1;
		u32 hlos : 1;
		u32 rsvd3 : 16;
	} aerces;
	struct {
		u32 rem : 1;
		u32 rsvd : 5;
		u32 btm : 1;
		u32 bdm : 1;
		u32 rrm : 1;
		u32 rsvd2 : 3;
		u32 rtm : 1;
		u32 anfem : 1;
		u32 ciem : 1;
		u32 hlom : 1;
		u32 rsvd3 : 16;
	} aercem;
	struct {
		u32 fep : 5;
		u32 egc : 1;
		u32 ege : 1;
		u32 ecc : 1;
		u32 ece : 1;
		u32 mhrc : 1;
		u32 mhre : 1;
		u32 tplp : 1;
		u32 rsvd : 20;
	} aercc;
	struct {
		u8 hb3;
		u8 hb2;
		u8 hb1;
		u8 hb0;
		u8 hb7;
		u8 hb6;
		u8 hb5;
		u8 hb4;
		u8 hb11;
		u8 hb10;
		u8 hb9;
		u8 hb8;
		u8 hb15;
		u8 hb14;
		u8 hb13;
		u8 hb12;
	} aerhl;
	struct {
		u8 tpl1b3;
		u8 tpl1b2;
		u8 tpl1b1;
		u8 tpl1b0;
		u8 tpl2b3;
		u8 tpl2b2;
		u8 tpl2b1;
		u8 tpl2b0;
		u8 tpl3b3;
		u8 tpl3b2;
		u8 tpl3b1;
		u8 tpl3b0;
		u8 tpl4b3;
		u8 tpl4b2;
		u8 tpl4b1;
		u8 tpl4b0;
	} aertlp;
};

struct pci_ext_cap_dsn {
	struct pci_ext_cap id;
	u64 serial;
};

struct nvme_ctrl_regs {
	union {
		struct {
			u16 mqes;
			u16 cqr : 1;
			u16 ams : 2;
			u16 rsvd : 5;
			u16 to : 8;
			u16 dstrd : 4;
			u16 nssrs : 1;
			u16 css : 8;
			u16 rsvd2 : 3;
			u16 mpsmin : 4;
			u16 mpsmax : 4;
			u16 rsvd3 : 8;
		} cap;
		u64 u_cap;
	};
	//uint64_t			cap;	/* Controller Capabilities */
	union {
		struct {
			u8 rsvd;
			u8 mnr;
			u16 mjr;
		} vs;
		u32 u_vs;
	};
	//uint32_t			vs;	/* Version */
	u32 intms; /* Interrupt Mask Set */
	u32 intmc; /* Interrupt Mask Clear */
	union {
		struct {
			u16 en : 1;
			u16 rsvd : 3;
			u16 css : 3;
			u16 mps : 4;
			u16 ams : 3;
			u16 shn : 2;
			u16 iosqes : 4;
			u16 iocqes : 4;
			u16 rsvd2 : 8;
		} cc;
		u32 u_cc;
	};
	//uint32_t			cc;	/* Controller Configuration */
	u32 rsvd1; /* Reserved */
	union {
		struct {
			u32 rdy : 1;
			u32 cfs : 1;
			u32 shst : 2;
			u32 nssro : 1;
			u32 pp : 1;
			u32 rsvd : 26;
		} csts;
		u32 u_csts;
	};
	//uint32_t			csts;	/* Controller Status */
	u32 nssr; /* Subsystem Reset */
	union {
		struct {
			u32 asqs : 12;
			u32 rsvd1 : 4;
			u32 acqs : 12;
			u32 rsvd2 : 4;
		} aqa;
		u32 u_aqa;
	};
	//uint32_t			aqa;	/* Admin Queue Attributes */
	union {
		struct {
			u64 rsvd : 12;
			u64 asqb : 52;
		} asq;
		u64 u_asq;
	};
	//uint64_t			asq;	/* Admin SQ Base Address */
	union {
		struct {
			u64 rsvd : 12;
			u64 acqb : 52;
		} acq;
		u64 u_acq;
	};
	//uint64_t			acq;	/* Admin CQ Base Address */
	union {
		struct {
			u32 bir : 3;
			u32 rsvd : 9;
			u32 ofst : 20;
		} cmbloc;
		u32 u_cmbloc;
	};
	//uint32_t			cmbloc; /* Controller Memory Buffer Location */
	union {
		struct {
			u32 sqs : 1;
			u32 cqs : 1;
			u32 lists : 1;
			u32 rds : 1;
			u32 wds : 1;
			u32 rsvd : 3;
			u32 szu : 4;
			u32 sz : 20;
		} cmbsz;
		u32 u_cmbsz;
	};
	//uint32_t			cmbsz;  /* Controller Memory Buffer Size */
};


#define NVMEV_PCI_DOMAIN_NUM 0x0001
#define NVMEV_PCI_BUS_NUM 0x10

//[PCI_HEADER][PM_CAP][MSIX_CAP][PCIE_CAP] | [AER_CAP][EXT_CAP]
#define SZ_PCI_HDR sizeof(struct pci_header) // 0
#define SZ_PCI_PM_CAP sizeof(struct pci_pm_cap) // 0x40
#define SZ_PCI_MSIX_CAP sizeof(struct pci_msix_cap) // 0x50
#define SZ_PCIE_CAP sizeof(struct pcie_cap) // 0x60

#define OFFS_PCI_HDR 0x0
#define OFFS_PCI_PM_CAP 0x40
#define OFFS_PCI_MSIX_CAP 0x50
#define OFFS_PCIE_CAP 0x60

#define SZ_HEADER (OFFS_PCIE_CAP + SZ_PCIE_CAP)

//#define PCI_CFG_SPACE_SIZE 0x100
#define PCI_EXT_CAP_START 0x50

#define OFFS_PCI_EXT_CAP (PCI_CFG_SPACE_SIZE)


enum {
	CAP_CSS_BIT_NVM = (1 << 0),
	CAP_CSS_BIT_SPECIFIC = (1 << 6),
	CAP_CSS_BIT_NOT_SUPPORTED = (1 << 7),
};

#endif /* _LIB_NVMEV_HDR_H */

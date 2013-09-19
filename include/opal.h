/* (C) Copyright IBM Corp., 2013 and provided pursuant to the Technology
 * Licensing Agreement between Google Inc. and International Business
 * Machines Corporation, IBM License Reference Number AA130103030256 and
 * confidentiality governed by the Parties’ Mutual Nondisclosure Agreement
 * number V032404DR, executed by the parties on November 6, 2007, and
 * Supplement V032404DR-3 dated August 16, 2012 (the “NDA”). */
#ifndef __OPAL_H
#define __OPAL_H

/****** OPAL APIs ******/

/* Return codes */
#define OPAL_SUCCESS 		0
#define OPAL_PARAMETER		-1
#define OPAL_BUSY		-2
#define OPAL_PARTIAL		-3
#define OPAL_CONSTRAINED	-4
#define OPAL_CLOSED		-5
#define OPAL_HARDWARE		-6
#define OPAL_UNSUPPORTED	-7
#define OPAL_PERMISSION		-8
#define OPAL_NO_MEM		-9
#define OPAL_RESOURCE		-10
#define OPAL_INTERNAL_ERROR	-11
#define OPAL_BUSY_EVENT		-12
#define OPAL_HARDWARE_FROZEN	-13
#define OPAL_WRONG_STATE	-14

/* API Tokens (in r0) */
#define OPAL_TEST				0
#define OPAL_CONSOLE_WRITE			1
#define OPAL_CONSOLE_READ			2
#define OPAL_RTC_READ				3
#define OPAL_RTC_WRITE				4
#define OPAL_CEC_POWER_DOWN			5
#define OPAL_CEC_REBOOT				6
#define OPAL_READ_NVRAM				7
#define OPAL_WRITE_NVRAM			8
#define OPAL_HANDLE_INTERRUPT			9
#define OPAL_POLL_EVENTS			10
#define OPAL_PCI_SET_HUB_TCE_MEMORY		11
#define OPAL_PCI_SET_PHB_TCE_MEMORY		12
#define OPAL_PCI_CONFIG_READ_BYTE		13
#define OPAL_PCI_CONFIG_READ_HALF_WORD  	14
#define OPAL_PCI_CONFIG_READ_WORD		15
#define OPAL_PCI_CONFIG_WRITE_BYTE		16
#define OPAL_PCI_CONFIG_WRITE_HALF_WORD		17
#define OPAL_PCI_CONFIG_WRITE_WORD		18
#define OPAL_SET_XIVE				19
#define OPAL_GET_XIVE				20
#define OPAL_GET_COMPLETION_TOKEN_STATUS	21 /* obsolete */
#define OPAL_REGISTER_OPAL_EXCEPTION_HANDLER	22
#define OPAL_PCI_EEH_FREEZE_STATUS		23
#define OPAL_PCI_SHPC				24
#define OPAL_CONSOLE_WRITE_BUFFER_SPACE		25
#define OPAL_PCI_EEH_FREEZE_CLEAR		26
#define OPAL_PCI_PHB_MMIO_ENABLE		27
#define OPAL_PCI_SET_PHB_MEM_WINDOW		28
#define OPAL_PCI_MAP_PE_MMIO_WINDOW		29
#define OPAL_PCI_SET_PHB_TABLE_MEMORY		30
#define OPAL_PCI_SET_PE				31
#define OPAL_PCI_SET_PELTV			32
#define OPAL_PCI_SET_MVE			33
#define OPAL_PCI_SET_MVE_ENABLE			34
#define OPAL_PCI_GET_XIVE_REISSUE		35
#define OPAL_PCI_SET_XIVE_REISSUE		36
#define OPAL_PCI_SET_XIVE_PE			37
#define OPAL_GET_XIVE_SOURCE			38
#define OPAL_GET_MSI_32				39
#define OPAL_GET_MSI_64				40
#define OPAL_START_CPU				41
#define OPAL_QUERY_CPU_STATUS			42
#define OPAL_WRITE_OPPANEL			43
#define OPAL_PCI_MAP_PE_DMA_WINDOW		44
#define OPAL_PCI_MAP_PE_DMA_WINDOW_REAL		45
#define OPAL_PCI_RESET				49
#define OPAL_PCI_GET_HUB_DIAG_DATA		50
#define OPAL_PCI_GET_PHB_DIAG_DATA		51
#define OPAL_PCI_FENCE_PHB			52
#define OPAL_PCI_REINIT				53
#define OPAL_PCI_MASK_PE_ERROR			54
#define OPAL_SET_SLOT_LED_STATUS		55
#define OPAL_GET_EPOW_STATUS			56
#define OPAL_SET_SYSTEM_ATTENTION_LED		57
#define OPAL_RESERVED1				58
#define OPAL_RESERVED2				59
#define OPAL_PCI_NEXT_ERROR			60
#define OPAL_PCI_EEH_FREEZE_STATUS2		61
#define OPAL_PCI_POLL				62
#define OPAL_PCI_MSI_EOI			63
#define OPAL_PCI_GET_PHB_DIAG_DATA2		64
#define OPAL_XSCOM_READ				65
#define OPAL_XSCOM_WRITE			66
#define OPAL_LPC_READ				67
#define OPAL_LPC_WRITE				68
#define OPAL_RETURN_CPU				69
#define OPAL_ELOG_READ				70
#define OPAL_ELOG_WRITE				71
#define OPAL_ELOG_ACK				72
#define OPAL_ELOG_RESEND			73
#define OPAL_ELOG_SIZE				74
#define OPAL_LAST				74

#ifndef __ASSEMBLY__

#include <compiler.h>
#include <stdint.h>

/* Other enums */

enum OpalVendorApiTokens {
	OPAL_START_VENDOR_API_RANGE = 1000, OPAL_END_VENDOR_API_RANGE = 1999
};

enum OpalFreezeState {
	OPAL_EEH_STOPPED_NOT_FROZEN = 0,
	OPAL_EEH_STOPPED_MMIO_FREEZE = 1,
	OPAL_EEH_STOPPED_DMA_FREEZE = 2,
	OPAL_EEH_STOPPED_MMIO_DMA_FREEZE = 3,
	OPAL_EEH_STOPPED_RESET = 4,
	OPAL_EEH_STOPPED_TEMP_UNAVAIL = 5,
	OPAL_EEH_STOPPED_PERM_UNAVAIL = 6
};
enum OpalEehFreezeActionToken {
	OPAL_EEH_ACTION_CLEAR_FREEZE_MMIO = 1,
	OPAL_EEH_ACTION_CLEAR_FREEZE_DMA = 2,
	OPAL_EEH_ACTION_CLEAR_FREEZE_ALL = 3
};

enum OpalPciStatusToken {
	OPAL_EEH_NO_ERROR	= 0,
	OPAL_EEH_IOC_ERROR	= 1,
	OPAL_EEH_PHB_ERROR	= 2,
	OPAL_EEH_PE_ERROR	= 3,
	OPAL_EEH_PE_MMIO_ERROR	= 4,
	OPAL_EEH_PE_DMA_ERROR	= 5
};

enum OpalPciErrorSeverity {
	OPAL_EEH_SEV_NO_ERROR	= 0,
	OPAL_EEH_SEV_IOC_DEAD	= 1,
	OPAL_EEH_SEV_PHB_DEAD	= 2,
	OPAL_EEH_SEV_PHB_FENCED	= 3,
	OPAL_EEH_SEV_PE_ER	= 4,
	OPAL_EEH_SEV_INF	= 5
};

enum OpalShpcAction {
	OPAL_SHPC_GET_LINK_STATE = 0,
	OPAL_SHPC_GET_SLOT_STATE = 1
};

enum OpalShpcLinkState {
	OPAL_SHPC_LINK_DOWN = 0,
	OPAL_SHPC_LINK_UP_x1 = 1,
	OPAL_SHPC_LINK_UP_x2 = 2,
	OPAL_SHPC_LINK_UP_x4 = 4,
	OPAL_SHPC_LINK_UP_x8 = 8,
	OPAL_SHPC_LINK_UP_x16 = 16,
	OPAL_SHPC_LINK_UP_x32 = 32
};
enum OpalMmioWindowType {
	OPAL_M32_WINDOW_TYPE = 1,
	OPAL_M64_WINDOW_TYPE = 2,
	OPAL_IO_WINDOW_TYPE = 3
};
enum OpalShpcSlotState {
	OPAL_SHPC_DEV_NOT_PRESENT = 0,
	OPAL_SHPC_DEV_PRESENT = 1
};
enum OpalShpcPowerState {
	OPAL_SHPC_POWER_OFF = 0,
	OPAL_SHPC_POWER_ON = 1
};
enum OpalExceptionHandler {
	OPAL_MACHINE_CHECK_HANDLER = 1,
	OPAL_HYPERVISOR_MAINTENANCE_HANDLER = 2,
	OPAL_SOFTPATCH_HANDLER = 3
};
enum OpalPendingState {
	OPAL_EVENT_OPAL_INTERNAL = 0x1,
	OPAL_EVENT_NVRAM = 0x2,
	OPAL_EVENT_RTC = 0x4,
	OPAL_EVENT_CONSOLE_OUTPUT = 0x8,
	OPAL_EVENT_CONSOLE_INPUT = 0x10,
	OPAL_EVENT_ERROR_LOG_AVAIL = 0x20,
	OPAL_EVENT_ERROR_LOG = 0x40,
	OPAL_EVENT_EPOW = 0x80,
	OPAL_EVENT_LED_STATUS = 0x100,
	OPAL_EVENT_PCI_ERROR = 0x200,
};

/* Machine check related definitions */
enum OpalMCE_Version {
	OpalMCE_V1 = 1,
};

enum OpalMCE_Severity {
	OpalMCE_SEV_NO_ERROR = 0,
	OpalMCE_SEV_WARNING = 1,
	OpalMCE_SEV_ERROR_SYNC = 2,
	OpalMCE_SEV_FATAL = 3,
};

enum OpalMCE_Disposition {
	OpalMCE_DISPOSITION_RECOVERED = 0,
	OpalMCE_DISPOSITION_NOT_RECOVERED = 1,
};

enum OpalMCE_Initiator {
	OpalMCE_INITIATOR_UNKNOWN = 0,
	OpalMCE_INITIATOR_CPU = 1,
};

enum OpalMCE_ErrorType {
	OpalMCE_ERROR_TYPE_UNKNOWN = 0,
	OpalMCE_ERROR_TYPE_UE = 1,
	OpalMCE_ERROR_TYPE_SLB = 2,
	OpalMCE_ERROR_TYPE_ERAT = 3,
	OpalMCE_ERROR_TYPE_TLB = 4,
};

enum OpalMCE_UeErrorType {
	OpalMCE_UE_ERROR_INDETERMINATE = 0,
	OpalMCE_UE_ERROR_IFETCH = 1,
	OpalMCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH = 2,
	OpalMCE_UE_ERROR_LOAD_STORE = 3,
	OpalMCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE = 4,
};

enum OpalMCE_SlbErrorType {
	OpalMCE_SLB_ERROR_INDETERMINATE = 0,
	OpalMCE_SLB_ERROR_PARITY = 1,
	OpalMCE_SLB_ERROR_MULTIHIT = 2,
};

enum OpalMCE_EratErrorType {
	OpalMCE_ERAT_ERROR_INDETERMINATE = 0,
	OpalMCE_ERAT_ERROR_PARITY = 1,
	OpalMCE_ERAT_ERROR_MULTIHIT = 2,
};

enum OpalMCE_TlbErrorType {
	OpalMCE_TLB_ERROR_INDETERMINATE = 0,
	OpalMCE_TLB_ERROR_PARITY = 1,
	OpalMCE_TLB_ERROR_MULTIHIT = 2,
};

enum OpalThreadStatus {
	OPAL_THREAD_INACTIVE = 0x0,
	OPAL_THREAD_STARTED = 0x1
};

enum OpalPciBusCompare {
	OpalPciBusAny	= 0,	/* Any bus number match */
	OpalPciBus3Bits	= 2,	/* Match top 3 bits of bus number */
	OpalPciBus4Bits	= 3,	/* Match top 4 bits of bus number */
	OpalPciBus5Bits	= 4,	/* Match top 5 bits of bus number */
	OpalPciBus6Bits	= 5,	/* Match top 6 bits of bus number */
	OpalPciBus7Bits	= 6,	/* Match top 7 bits of bus number */
	OpalPciBusAll	= 7,	/* Match bus number exactly */
};

enum OpalDeviceCompare {
	OPAL_IGNORE_RID_DEVICE_NUMBER = 0,
	OPAL_COMPARE_RID_DEVICE_NUMBER = 1
};

enum OpalFuncCompare {
	OPAL_IGNORE_RID_FUNCTION_NUMBER = 0,
	OPAL_COMPARE_RID_FUNCTION_NUMBER = 1
};

enum OpalPeAction {
	OPAL_UNMAP_PE = 0,
	OPAL_MAP_PE = 1
};

enum OpalPeltvAction {
	OPAL_REMOVE_PE_FROM_DOMAIN = 0,
	OPAL_ADD_PE_TO_DOMAIN = 1
};

enum OpalMveEnableAction {
	OPAL_DISABLE_MVE = 0,
	OPAL_ENABLE_MVE = 1
};

enum OpalM64Action {
	OPAL_DISABLE_M64 = 0,
	OPAL_ENABLE_M64_SPLIT = 1,
	OPAL_ENABLE_M64_NON_SPLIT = 2
};

enum OpalPciResetAndReinitScope {
	OPAL_PHB_COMPLETE		= 1,
	OPAL_PCI_LINK			= 2,
	OPAL_PHB_ERROR			= 3,
	OPAL_PCI_HOT_RESET		= 4,
	OPAL_PCI_FUNDAMENTAL_RESET 	= 5,
	OPAL_PCI_IODA_TABLE_RESET 	= 6
};

enum OpalPciResetState {
	OPAL_DEASSERT_RESET	= 0,
	OPAL_ASSERT_RESET	= 1
};

enum OpalPciMaskAction {
	OPAL_UNMASK_ERROR_TYPE = 0,
	OPAL_MASK_ERROR_TYPE = 1
};

enum OpalSlotLedType {
	OPAL_SLOT_LED_ID_TYPE = 0,
	OPAL_SLOT_LED_FAULT_TYPE = 1
};

enum OpalLedAction {
	OPAL_TURN_OFF_LED = 0,
	OPAL_TURN_ON_LED = 1,
	OPAL_QUERY_LED_STATE_AFTER_BUSY = 2
};

enum OpalEpowStatus {
	OPAL_EPOW_NONE = 0,
	OPAL_EPOW_UPS = 1,
	OPAL_EPOW_OVER_AMBIENT_TEMP = 2,
	OPAL_EPOW_OVER_INTERNAL_TEMP = 3
};

/*
 * Address cycle types for LPC accesses. These also correspond
 * to the content of the first cell of the "reg" property for
 * device nodes on the LPC bus
 */
enum OpalLPCAddressType {
	OPAL_LPC_MEM	= 0,
	OPAL_LPC_IO	= 1,
	OPAL_LPC_FW	= 2,
};

struct opal_machine_check_event {
	enum OpalMCE_Version	version:8;	/* 0x00 */
	uint8_t			in_use;		/* 0x01 */
	enum OpalMCE_Severity	severity:8;	/* 0x02 */
	enum OpalMCE_Initiator	initiator:8;	/* 0x03 */
	enum OpalMCE_ErrorType	error_type:8;	/* 0x04 */
	enum OpalMCE_Disposition disposition:8; /* 0x05 */
	uint8_t			reserved_1[2];	/* 0x06 */
	uint64_t		gpr3;		/* 0x08 */
	uint64_t		srr0;		/* 0x10 */
	uint64_t		srr1;		/* 0x18 */
	union {					/* 0x20 */
		struct {
			enum OpalMCE_UeErrorType ue_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		physical_address_provided;
			uint8_t		reserved_1[5];
			uint64_t	effective_address;
			uint64_t	physical_address;
			uint8_t		reserved_2[8];
		} ue_error;

		struct {
			enum OpalMCE_SlbErrorType slb_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} slb_error;

		struct {
			enum OpalMCE_EratErrorType erat_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} erat_error;

		struct {
			enum OpalMCE_TlbErrorType tlb_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} tlb_error;
	} u;
};

enum {
	OPAL_P7IOC_DIAG_TYPE_NONE	= 0,
	OPAL_P7IOC_DIAG_TYPE_RGC	= 1,
	OPAL_P7IOC_DIAG_TYPE_BI		= 2,
	OPAL_P7IOC_DIAG_TYPE_CI		= 3,
	OPAL_P7IOC_DIAG_TYPE_MISC	= 4,
	OPAL_P7IOC_DIAG_TYPE_I2C	= 5,
	OPAL_P7IOC_DIAG_TYPE_LAST	= 6
};

struct OpalIoP7IOCErrorData {
	uint16_t type;

	/* GEM */
	uint64_t gemXfir;
	uint64_t gemRfir;
	uint64_t gemRirqfir;
	uint64_t gemMask;
	uint64_t gemRwof;

	/* LEM */
	uint64_t lemFir;
	uint64_t lemErrMask;
	uint64_t lemAction0;
	uint64_t lemAction1;
	uint64_t lemWof;

	union {
		struct OpalIoP7IOCRgcErrorData {
			uint64_t rgcStatus;		/* 3E1C10 */
			uint64_t rgcLdcp;		/* 3E1C18 */
		}rgc;
		struct OpalIoP7IOCBiErrorData {
			uint64_t biLdcp0;		/* 3C0100, 3C0118 */
			uint64_t biLdcp1;		/* 3C0108, 3C0120 */
			uint64_t biLdcp2;		/* 3C0110, 3C0128 */
			uint64_t biFenceStatus;		/* 3C0130, 3C0130 */

			uint8_t  biDownbound;		/* BI Downbound or Upbound */
		}bi;
		struct OpalIoP7IOCCiErrorData {
			uint64_t ciPortStatus;		/* 3Dn008 */
			uint64_t ciPortLdcp;		/* 3Dn010 */

			uint8_t  ciPort;		/* Index of CI port: 0/1 */
		}ci;
	};
};

/**
 * This structure defines the overlay which will be used to store PHB error
 * data upon request.
 */
enum {
	OPAL_PHB_ERROR_DATA_VERSION_1 = 1,
};

enum {
	OPAL_PHB_ERROR_DATA_TYPE_P7IOC = 1,
	OPAL_PHB_ERROR_DATA_TYPE_PHB3 = 2
};

enum {
	OPAL_P7IOC_NUM_PEST_REGS = 128,
	OPAL_PHB3_NUM_PEST_REGS = 256
};

struct OpalIoPhbErrorCommon {
	uint32_t version;
	uint32_t ioType;
	uint32_t len;
};

struct OpalIoP7IOCPhbErrorData {
	struct OpalIoPhbErrorCommon common;

	uint32_t brdgCtl;

	// P7IOC utl regs
	uint32_t portStatusReg;
	uint32_t rootCmplxStatus;
	uint32_t busAgentStatus;

	// P7IOC cfg regs
	uint32_t deviceStatus;
	uint32_t slotStatus;
	uint32_t linkStatus;
	uint32_t devCmdStatus;
	uint32_t devSecStatus;

	// cfg AER regs
	uint32_t rootErrorStatus;
	uint32_t uncorrErrorStatus;
	uint32_t corrErrorStatus;
	uint32_t tlpHdr1;
	uint32_t tlpHdr2;
	uint32_t tlpHdr3;
	uint32_t tlpHdr4;
	uint32_t sourceId;

	uint32_t rsv3;

	// Record data about the call to allocate a buffer.
	uint64_t errorClass;
	uint64_t correlator;

	//P7IOC MMIO Error Regs
	uint64_t p7iocPlssr;                // n120
	uint64_t p7iocCsr;                  // n110
	uint64_t lemFir;                    // nC00
	uint64_t lemErrorMask;              // nC18
	uint64_t lemWOF;                    // nC40
	uint64_t phbErrorStatus;            // nC80
	uint64_t phbFirstErrorStatus;       // nC88
	uint64_t phbErrorLog0;              // nCC0
	uint64_t phbErrorLog1;              // nCC8
	uint64_t mmioErrorStatus;           // nD00
	uint64_t mmioFirstErrorStatus;      // nD08
	uint64_t mmioErrorLog0;             // nD40
	uint64_t mmioErrorLog1;             // nD48
	uint64_t dma0ErrorStatus;           // nD80
	uint64_t dma0FirstErrorStatus;      // nD88
	uint64_t dma0ErrorLog0;             // nDC0
	uint64_t dma0ErrorLog1;             // nDC8
	uint64_t dma1ErrorStatus;           // nE00
	uint64_t dma1FirstErrorStatus;      // nE08
	uint64_t dma1ErrorLog0;             // nE40
	uint64_t dma1ErrorLog1;             // nE48
	uint64_t pestA[OPAL_P7IOC_NUM_PEST_REGS];
	uint64_t pestB[OPAL_P7IOC_NUM_PEST_REGS];
};

struct OpalIoPhb3ErrorData {
	struct OpalIoPhbErrorCommon common;

	uint32_t brdgCtl;

	/* PHB3 UTL regs */
	uint32_t portStatusReg;
	uint32_t rootCmplxStatus;
	uint32_t busAgentStatus;

	/* PHB3 cfg regs */
	uint32_t deviceStatus;
	uint32_t slotStatus;
	uint32_t linkStatus;
	uint32_t devCmdStatus;
	uint32_t devSecStatus;

	/* cfg AER regs */
	uint32_t rootErrorStatus;
	uint32_t uncorrErrorStatus;
	uint32_t corrErrorStatus;
	uint32_t tlpHdr1;
	uint32_t tlpHdr2;
	uint32_t tlpHdr3;
	uint32_t tlpHdr4;
	uint32_t sourceId;

	uint32_t rsv3;

	/* Record data about the call to allocate a buffer */
	uint64_t errorClass;
	uint64_t correlator;

	/* PHB3 MMIO Error Regs */
	uint64_t nFir;			/* 000 */
	uint64_t nFirMask;		/* 003 */
	uint64_t nFirWOF;		/* 008 */
	uint64_t phbPlssr;		/* 120 */
	uint64_t phbCsr;		/* 110 */
	uint64_t lemFir;		/* C00 */
	uint64_t lemErrorMask;		/* C18 */
	uint64_t lemWOF;		/* C40 */
	uint64_t phbErrorStatus;	/* C80 */
	uint64_t phbFirstErrorStatus;	/* C88 */
	uint64_t phbErrorLog0;		/* CC0 */
	uint64_t phbErrorLog1;		/* CC8 */
	uint64_t mmioErrorStatus;	/* D00 */
	uint64_t mmioFirstErrorStatus;	/* D08 */
	uint64_t mmioErrorLog0;		/* D40 */
	uint64_t mmioErrorLog1;		/* D48 */
	uint64_t dma0ErrorStatus;	/* D80 */
	uint64_t dma0FirstErrorStatus;	/* D88 */
	uint64_t dma0ErrorLog0;		/* DC0 */
	uint64_t dma0ErrorLog1;		/* DC8 */
	uint64_t dma1ErrorStatus;	/* E00 */
	uint64_t dma1FirstErrorStatus;	/* E08 */
	uint64_t dma1ErrorLog0;		/* E40 */
	uint64_t dma1ErrorLog1;		/* E48 */
	uint64_t pestA[OPAL_PHB3_NUM_PEST_REGS];
	uint64_t pestB[OPAL_PHB3_NUM_PEST_REGS];
};

typedef struct oppanel_line {
	const char * 	line;
	uint64_t 	line_len;
} oppanel_line_t;

/****** Internal **********/

/* An opal table entry */
struct opal_table_entry {
	void		*func;
	uint32_t	token;
	uint32_t	nargs;
};

#define opal_call(__tok, __func, __nargs)				\
static struct opal_table_entry __e_##__func __used __section(".opal_table") = \
{ .func = __func, .token = __tok,					\
  .nargs = __nargs + 0 * sizeof(__func( __test_args##__nargs )) }

/* Make sure function takes args they claim.  Look away now... */
#define __test_args0
#define __test_args1 0
#define __test_args2 0,0
#define __test_args3 0,0,0
#define __test_args4 0,0,0,0
#define __test_args5 0,0,0,0,0
#define __test_args6 0,0,0,0,0,0
#define __test_args7 0,0,0,0,0,0,0

extern struct opal_table_entry __opal_table_start[];
extern struct opal_table_entry __opal_table_end[];

extern uint64_t opal_pending_events;

extern void opal_table_init(void);
extern void opal_update_pending_evt(uint64_t evt_mask, uint64_t evt_values);
extern void add_opal_nodes(void);
#define opal_register(token, func, nargs)				\
	__opal_register((token) + 0*sizeof(func(__test_args##nargs)),	\
			(func), (nargs))
extern void __opal_register(uint64_t token, void *func, unsigned num_args);

/* Warning: no locking at the moment, do at init time only
 *
 * XXX TODO: Add the big RCU-ish "opal API lock" to protect us here
 * which will also be used for other things such as runtime updates
 */
extern void opal_add_poller(void (*poller)(void *data), void *data);
extern void opal_del_poller(void (*poller)(void *data));


#endif /* __ASSEMBLY__ */
#endif /* __OPAL_H */

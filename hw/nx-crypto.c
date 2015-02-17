/* Copyright 2015 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <skiboot.h>
#include <xscom.h>
#include <io.h>
#include <cpu.h>
#include <nx.h>

/* Configuration settings  */
#define CFG_SYM_FC_ENABLE	(0) /* disable all sym functions */
#define CFG_SYM_ENABLE		(0) /* disable sym engines */
#define CFG_ASYM_FC_ENABLE	(0) /* disable all asym functions */
#define CFG_ASYM_ENABLE		(0) /* disable asym engines */
#define AES_SHA_MAX_RR		(1) /* valid range: 1-8 */
#define AES_SHA_CSB_WR		NX_DMA_CSB_WR_PDMA
#define AES_SHA_COMPLETION_MODE	NX_DMA_COMPLETION_MODE_PDMA
#define AES_SHA_CPB_WR		NX_DMA_CPB_WR_DMA_NOPAD
#define AES_SHA_OUTPUT_DATA_WR	NX_DMA_OUTPUT_DATA_WR_DMA
#define AMF_MAX_RR		(1) /* valid range: 1-8 */
#define AMF_CSB_WR		NX_DMA_CSB_WR_PDMA
#define AMF_COMPLETION_MODE	NX_DMA_COMPLETION_MODE_PDMA
#define AMF_CPB_WR		(0) /* CPB WR not done with AMF */
#define AMF_OUTPUT_DATA_WR	NX_DMA_OUTPUT_DATA_WR_DMA
#define EE_AMF_0		(0) /* disable AMF engine 0 */
#define EE_AMF_1		(0) /* disable AMF engine 1 */
#define EE_AMF_2		(0) /* disable AMF engine 2 */
#define EE_AMF_3		(0) /* disable AMF engine 3 */
#define EE_SYM_0		(0) /* disable SYM engine 0 */
#define EE_SYM_1		(0) /* disable SYM engine 1 */

/* counters used to provide unique Coprocessor Instance numbers */
static u64 nx_sym_ci_counter = 1;
static u64 nx_asym_ci_counter = 1;

static int nx_cfg_sym(u32 gcid, u64 xcfg, u64 instance)
{
	u64 cfg, ci, ct;
	int rc;

	if (instance > NX_P8_SYM_CFG_CI_MAX) {
		prerror("NX%d: ERROR: SYM CI %u exceeds max %u\n",
			gcid, (unsigned int)instance, NX_P8_SYM_CFG_CI_MAX);
		return OPAL_INTERNAL_ERROR;
	}

	rc = xscom_read(gcid, xcfg, &cfg);
	if (rc)
		return rc;

	ct = GETFIELD(NX_P8_SYM_CFG_CT, cfg);
	if (!ct)
		prlog(PR_INFO, "NX%d:   SYM CT set to %u\n", gcid, NX_CT_SYM);
	else if (ct == NX_CT_SYM)
		prlog(PR_INFO, "NX%d:   SYM CT already set to %u\n",
		      gcid, NX_CT_SYM);
	else
		prlog(PR_INFO, "NX%d:   SYM CT already set to %u, "
		      "changing to %u\n", gcid, (unsigned int)ct, NX_CT_SYM);
	ct = NX_CT_SYM;
	cfg = SETFIELD(NX_P8_SYM_CFG_CT, cfg, ct);

	/* Coprocessor Instance must be shifted left.
	 * See hw doc Section 5.5.1.
	 */
	ci = GETFIELD(NX_P8_SYM_CFG_CI, cfg) >> NX_P8_SYM_CFG_CI_LSHIFT;
	if (!ci)
		prlog(PR_INFO, "NX%d:   SYM CI set to %u\n", gcid,
		      (unsigned int)instance);
	else if (ci == instance)
		prlog(PR_INFO, "NX%d:   SYM CI already set to %u\n", gcid,
		      (unsigned int)instance);
	else
		prlog(PR_INFO, "NX%d:   SYM CI already set to %u, "
		      "changing to %u\n", gcid,
		      (unsigned int)ci, (unsigned int)instance);
	ci = instance;
	cfg = SETFIELD(NX_P8_SYM_CFG_CI, cfg, ci << NX_P8_SYM_CFG_CI_LSHIFT);

	cfg = SETFIELD(NX_P8_SYM_CFG_FC_ENABLE, cfg, CFG_SYM_FC_ENABLE);

	cfg = SETFIELD(NX_P8_SYM_CFG_ENABLE, cfg, CFG_SYM_ENABLE);

	rc = xscom_write(gcid, xcfg, cfg);
	if (rc)
		prerror("NX%d: ERROR: SYM CT %u CI %u config failure %d\n",
			gcid, (unsigned int)ct, (unsigned int)ci, rc);
	else
		prlog(PR_DEBUG, "NX%d:   SYM Config 0x%016lx\n",
		      gcid, (unsigned long)cfg);

	return rc;
}

static int nx_cfg_asym(u32 gcid, u64 xcfg, u64 instance)
{
	u64 cfg, ci, ct;
	int rc;

	if (instance > NX_P8_ASYM_CFG_CI_MAX) {
		prerror("NX%d: ERROR: ASYM CI %u exceeds max %u\n",
			gcid, (unsigned int)instance, NX_P8_ASYM_CFG_CI_MAX);
		return OPAL_INTERNAL_ERROR;
	}

	rc = xscom_read(gcid, xcfg, &cfg);
	if (rc)
		return rc;

	ct = GETFIELD(NX_P8_ASYM_CFG_CT, cfg);
	if (!ct)
		prlog(PR_INFO, "NX%d:   ASYM CT set to %u\n",
		      gcid, NX_CT_ASYM);
	else if (ct == NX_CT_ASYM)
		prlog(PR_INFO, "NX%d:   ASYM CT already set to %u\n",
		      gcid, NX_CT_ASYM);
	else
		prlog(PR_INFO, "NX%d:   ASYM CT already set to %u, "
		      "changing to %u\n", gcid, (unsigned int)ct, NX_CT_ASYM);
	ct = NX_CT_ASYM;
	cfg = SETFIELD(NX_P8_ASYM_CFG_CT, cfg, ct);

	/* Coprocessor Instance must be shifted left.
	 * See hw doc Section 5.5.1.
	 */
	ci = GETFIELD(NX_P8_ASYM_CFG_CI, cfg) >> NX_P8_ASYM_CFG_CI_LSHIFT;
	if (!ci)
		prlog(PR_INFO, "NX%d:   ASYM CI set to %u\n", gcid,
		      (unsigned int)instance);
	else if (ci == instance)
		prlog(PR_INFO, "NX%d:   ASYM CI already set to %u\n", gcid,
		      (unsigned int)instance);
	else
		prlog(PR_INFO, "NX%d:   ASYM CI already set to %u, "
		      "changing to %u\n", gcid,
		      (unsigned int)ci, (unsigned int)instance);
	ci = instance;
	cfg = SETFIELD(NX_P8_ASYM_CFG_CI, cfg, ci << NX_P8_ASYM_CFG_CI_LSHIFT);

	cfg = SETFIELD(NX_P8_ASYM_CFG_FC_ENABLE, cfg, CFG_ASYM_FC_ENABLE);

	cfg = SETFIELD(NX_P8_ASYM_CFG_ENABLE, cfg, CFG_ASYM_ENABLE);

	rc = xscom_write(gcid, xcfg, cfg);
	if (rc)
		prerror("NX%d: ERROR: ASYM CT %u CI %u config failure %d\n",
			gcid, (unsigned int)ct, (unsigned int)ci, rc);
	else
		prlog(PR_DEBUG, "NX%d:   ASYM Config 0x%016lx\n",
		      gcid, (unsigned long)cfg);

	return rc;
}

static int nx_cfg_dma(u32 gcid, u64 xcfg)
{
	u64 cfg;
	int rc;

	rc = xscom_read(gcid, xcfg, &cfg);
	if (rc)
		return rc;

	cfg = SETFIELD(NX_P8_DMA_CFG_AES_SHA_MAX_RR, cfg,
		       AES_SHA_MAX_RR);
	cfg = SETFIELD(NX_P8_DMA_CFG_AES_SHA_CSB_WR, cfg,
		       AES_SHA_CSB_WR);
	cfg = SETFIELD(NX_P8_DMA_CFG_AES_SHA_COMPLETION_MODE, cfg,
		       AES_SHA_COMPLETION_MODE);
	cfg = SETFIELD(NX_P8_DMA_CFG_AES_SHA_CPB_WR, cfg,
		       AES_SHA_CPB_WR);
	cfg = SETFIELD(NX_P8_DMA_CFG_AES_SHA_OUTPUT_DATA_WR, cfg,
		       AES_SHA_OUTPUT_DATA_WR);

	cfg = SETFIELD(NX_P8_DMA_CFG_AMF_MAX_RR, cfg,
		       AMF_MAX_RR);
	cfg = SETFIELD(NX_P8_DMA_CFG_AMF_CSB_WR, cfg,
		       AMF_CSB_WR);
	cfg = SETFIELD(NX_P8_DMA_CFG_AMF_COMPLETION_MODE, cfg,
		       AMF_COMPLETION_MODE);
	cfg = SETFIELD(NX_P8_DMA_CFG_AMF_CPB_WR, cfg,
		       AMF_CPB_WR);
	cfg = SETFIELD(NX_P8_DMA_CFG_AMF_OUTPUT_DATA_WR, cfg,
		       AMF_OUTPUT_DATA_WR);

	rc = xscom_write(gcid, xcfg, cfg);
	if (rc)
		prerror("NX%d: ERROR: DMA config failure %d\n", gcid, rc);
	else
		prlog(PR_DEBUG, "NX%d:   DMA 0x%016lx\n", gcid,
		      (unsigned long)cfg);

	return rc;
}

static int nx_cfg_ee(u32 gcid, u64 xcfg)
{
	u64 cfg;
	int rc;

	rc = xscom_read(gcid, xcfg, &cfg);
	if (rc)
		return rc;

	cfg = SETFIELD(NX_P8_EE_CFG_AMF_0, cfg, EE_AMF_0);
	cfg = SETFIELD(NX_P8_EE_CFG_AMF_1, cfg, EE_AMF_1);
	cfg = SETFIELD(NX_P8_EE_CFG_AMF_2, cfg, EE_AMF_2);
	cfg = SETFIELD(NX_P8_EE_CFG_AMF_3, cfg, EE_AMF_3);
	cfg = SETFIELD(NX_P8_EE_CFG_SYM_0, cfg, EE_SYM_0);
	cfg = SETFIELD(NX_P8_EE_CFG_SYM_1, cfg, EE_SYM_1);

	rc = xscom_write(gcid, xcfg, cfg);
	if (rc)
		prerror("NX%d: ERROR: Engine Enable failure %d\n", gcid, rc);
	else
		prlog(PR_DEBUG, "NX%d:   Engine Enable 0x%016lx\n",
		      gcid, (unsigned long)cfg);

	return rc;
}

void nx_create_crypto_node(struct dt_node *node)
{
	u32 gcid;
	u32 pb_base;
	u64 cfg_dma, cfg_sym, cfg_asym, cfg_ee;
	int rc;

	gcid = dt_get_chip_id(node);
	pb_base = dt_get_address(node, 0, NULL);

	prlog(PR_INFO, "NX%d: Crypto at 0x%x\n", gcid, pb_base);

	if (dt_node_is_compatible(node, "ibm,power7-nx")) {
		prerror("NX%d: ERROR: Crypto not supported on Power7\n", gcid);
		return;
	} else if (dt_node_is_compatible(node, "ibm,power8-nx")) {
		cfg_dma = pb_base + NX_P8_DMA_CFG;
		cfg_sym = pb_base + NX_P8_SYM_CFG;
		cfg_asym = pb_base + NX_P8_ASYM_CFG;
		cfg_ee = pb_base + NX_P8_EE_CFG;
	} else {
		prerror("NX%d: ERROR: Unknown NX type!\n", gcid);
		return;
	}

	rc = nx_cfg_dma(gcid, cfg_dma);
	if (rc)
		return;

	rc = nx_cfg_sym(gcid, cfg_sym, nx_sym_ci_counter++);
	if (rc)
		return;

	rc = nx_cfg_asym(gcid, cfg_asym, nx_asym_ci_counter++);
	if (rc)
		return;

	rc = nx_cfg_ee(gcid, cfg_ee);
	if (rc)
		return;

	prlog(PR_INFO, "NX%d: Crypto Coprocessors Disabled (not supported)\n", gcid);
}

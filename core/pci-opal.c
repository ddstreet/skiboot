#include <skiboot.h>
#include <pci.h>
#include <pci-cfg.h>
#include <time.h>
#include <lock.h>

static int64_t opal_pci_config_read_byte(uint64_t phb_id,
					 uint64_t bus_dev_func,
					 uint64_t offset, uint8_t *data)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	phb->ops->lock(phb);
	rc = phb->ops->cfg_read8(phb, bus_dev_func, offset, data);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_CONFIG_READ_BYTE, opal_pci_config_read_byte);

static int64_t opal_pci_config_read_half_word(uint64_t phb_id,
					      uint64_t bus_dev_func,
					      uint64_t offset, uint16_t *data)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	phb->ops->lock(phb);
	rc = phb->ops->cfg_read16(phb, bus_dev_func, offset, data);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_CONFIG_READ_HALF_WORD, opal_pci_config_read_half_word);

static int64_t opal_pci_config_read_word(uint64_t phb_id,
					 uint64_t bus_dev_func,
					 uint64_t offset, uint32_t *data)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	phb->ops->lock(phb);
	rc = phb->ops->cfg_read32(phb, bus_dev_func, offset, data);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_CONFIG_READ_WORD, opal_pci_config_read_word);

static int64_t opal_pci_config_write_byte(uint64_t phb_id,
					  uint64_t bus_dev_func,
					  uint64_t offset, uint8_t data)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	phb->ops->lock(phb);
	rc = phb->ops->cfg_write8(phb, bus_dev_func, offset, data);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_CONFIG_WRITE_BYTE, opal_pci_config_write_byte);

static int64_t opal_pci_config_write_half_word(uint64_t phb_id,
					       uint64_t bus_dev_func,
					       uint64_t offset, uint16_t data)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	phb->ops->lock(phb);
	rc = phb->ops->cfg_write16(phb, bus_dev_func, offset, data);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_CONFIG_WRITE_HALF_WORD, opal_pci_config_write_half_word);

static int64_t opal_pci_config_write_word(uint64_t phb_id,
					  uint64_t bus_dev_func,
					  uint64_t offset, uint32_t data)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	phb->ops->lock(phb);
	rc = phb->ops->cfg_write32(phb, bus_dev_func, offset, data);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_CONFIG_WRITE_WORD, opal_pci_config_write_word);

static int64_t opal_pci_eeh_freeze_status(uint64_t phb_id, uint64_t pe_number,
					  uint8_t *freeze_state,
					  uint16_t *pci_error_type,
					  uint64_t *phb_status)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->eeh_freeze_status)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->eeh_freeze_status(phb, pe_number, freeze_state,
					 pci_error_type, NULL, phb_status);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_EEH_FREEZE_STATUS, opal_pci_eeh_freeze_status);

static int64_t opal_pci_eeh_freeze_clear(uint64_t phb_id, uint64_t pe_number,
					 uint64_t eeh_action_token)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->eeh_freeze_clear)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->eeh_freeze_clear(phb, pe_number, eeh_action_token);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_EEH_FREEZE_CLEAR, opal_pci_eeh_freeze_clear);

static int64_t opal_pci_phb_mmio_enable(uint64_t phb_id, uint16_t window_type,
					uint16_t window_num, uint16_t enable)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->phb_mmio_enable)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->phb_mmio_enable(phb, window_type, window_num, enable);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_PHB_MMIO_ENABLE, opal_pci_phb_mmio_enable);

static int64_t opal_pci_set_phb_mem_window(uint64_t phb_id,
					   uint16_t window_type,
					   uint16_t window_num,
					   uint64_t starting_real_address,
					   uint64_t starting_pci_address,
					   uint16_t segment_size)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->set_phb_mem_window)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->set_phb_mem_window(phb, window_type, window_num,
					  starting_real_address,
					  starting_pci_address,
					  segment_size);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_SET_PHB_MEM_WINDOW, opal_pci_set_phb_mem_window);

static int64_t opal_pci_map_pe_mmio_window(uint64_t phb_id, uint16_t pe_number,
					   uint16_t window_type,
					   uint16_t window_num,
					   uint16_t segment_num)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->map_pe_mmio_window)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->map_pe_mmio_window(phb, pe_number, window_type,
					  window_num, segment_num);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_MAP_PE_MMIO_WINDOW, opal_pci_map_pe_mmio_window);

static int64_t opal_pci_set_phb_table_memory(uint64_t phb_id __unused,
					     uint64_t rtt_addr __unused,
					     uint64_t ivt_addr __unused,
					     uint64_t ivt_len __unused,
					     uint64_t rej_array_addr __unused,
					     uint64_t peltv_addr __unused)
{
	/* IODA2 (P8) stuff, TODO */
	return OPAL_UNSUPPORTED;
}
opal_call(OPAL_PCI_SET_PHB_TABLE_MEMORY, opal_pci_set_phb_table_memory);

static int64_t opal_pci_set_pe(uint64_t phb_id, uint64_t pe_number,
			       uint64_t bus_dev_func, uint8_t bus_compare,
			       uint8_t dev_compare, uint8_t func_compare,
			       uint8_t pe_action)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->set_pe)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->set_pe(phb, pe_number, bus_dev_func, bus_compare,
			      dev_compare, func_compare, pe_action);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_SET_PE, opal_pci_set_pe);

static int64_t opal_pci_set_peltv(uint64_t phb_id, uint32_t parent_pe,
				  uint32_t child_pe, uint8_t state)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->set_peltv)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->set_peltv(phb, parent_pe, child_pe, state);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_SET_PELTV, opal_pci_set_peltv);

static int64_t opal_pci_set_mve(uint64_t phb_id, uint32_t mve_number,
				uint32_t pe_number)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->set_mve)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->set_mve(phb, mve_number, pe_number);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_SET_MVE, opal_pci_set_mve);

static int64_t opal_pci_set_mve_enable(uint64_t phb_id, uint32_t mve_number,
				       uint32_t state)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->set_mve_enable)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->set_mve_enable(phb, mve_number, state);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_SET_MVE_ENABLE, opal_pci_set_mve_enable);

static int64_t opal_pci_get_xive_reissue(uint64_t phb_id __unused,
					 uint32_t xive_number __unused,
					 uint8_t *p_bit __unused,
					 uint8_t *q_bit __unused)
{
	/* IODA2 (P8) stuff, TODO */
	return OPAL_UNSUPPORTED;
}
opal_call(OPAL_PCI_GET_XIVE_REISSUE, opal_pci_get_xive_reissue);

static int64_t opal_pci_set_xive_reissue(uint64_t phb_id __unused,
					 uint32_t xive_number __unused,
					 uint8_t p_bit __unused,
					 uint8_t q_bit __unused)
{
	/* IODA2 (P8) stuff, TODO */
	return OPAL_UNSUPPORTED;
}
opal_call(OPAL_PCI_SET_XIVE_REISSUE, opal_pci_set_xive_reissue);

static int64_t opal_pci_set_xive_pe(uint64_t phb_id, uint32_t pe_number,
				    uint32_t xive_num)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->set_xive_pe)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->set_xive_pe(phb, pe_number, xive_num);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_SET_XIVE_PE, opal_pci_set_xive_pe);

static int64_t opal_get_xive_source(uint64_t phb_id, uint32_t xive_num,
				    int32_t *interrupt_source_number)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->get_xive_source)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->get_xive_source(phb, xive_num, interrupt_source_number);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_GET_XIVE_SOURCE, opal_get_xive_source);

static int64_t opal_get_msi_32(uint64_t phb_id, uint32_t mve_number,
			       uint32_t xive_num, uint8_t msi_range,
			       uint32_t *msi_address, uint32_t *message_data)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->get_msi_32)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->get_msi_32(phb, mve_number, xive_num, msi_range,
				  msi_address, message_data);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_GET_MSI_32, opal_get_msi_32);

static int64_t opal_get_msi_64(uint64_t phb_id, uint32_t mve_number,
			       uint32_t xive_num, uint8_t msi_range,
			       uint64_t *msi_address, uint32_t *message_data)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->get_msi_64)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->get_msi_64(phb, mve_number, xive_num, msi_range,
				  msi_address, message_data);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_GET_MSI_64, opal_get_msi_64);

static int64_t opal_pci_map_pe_dma_window(uint64_t phb_id, uint16_t pe_number,
					  uint16_t window_id,
					  uint16_t tce_levels,
					  uint64_t tce_table_addr,
					  uint64_t tce_table_size,
					  uint64_t tce_page_size)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->map_pe_dma_window)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->map_pe_dma_window(phb, pe_number, window_id,
					 tce_levels, tce_table_addr,
					 tce_table_size, tce_page_size);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_MAP_PE_DMA_WINDOW, opal_pci_map_pe_dma_window);

static int64_t opal_pci_map_pe_dma_window_real(uint64_t phb_id,
					       uint16_t pe_number,
					       uint16_t window_id,
					       uint64_t pci_start_addr,
					       uint64_t pci_mem_size)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->map_pe_dma_window_real)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->map_pe_dma_window_real(phb, pe_number, window_id,
					      pci_start_addr, pci_mem_size);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_MAP_PE_DMA_WINDOW_REAL, opal_pci_map_pe_dma_window_real);

static int64_t opal_pci_reset(uint64_t phb_id, uint8_t reset_scope,
                              uint8_t assert_state)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc = OPAL_SUCCESS;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops)
		return OPAL_UNSUPPORTED;
	if (assert_state != OPAL_ASSERT_RESET &&
	    assert_state != OPAL_DEASSERT_RESET)
		return OPAL_PARAMETER;

	phb->ops->lock(phb);

	switch(reset_scope) {
	case OPAL_PHB_COMPLETE:
		if (!phb->ops->complete_reset) {
			rc = OPAL_UNSUPPORTED;
			break;
		}

		rc = phb->ops->complete_reset(phb, assert_state);
		if (rc < 0)
			prerror("PHB#%d: Failure on complete reset, rc=%lld\n",
				phb->opal_id, rc);
		break;
	case OPAL_PCI_FUNDAMENTAL_RESET:
		if (!phb->ops->fundamental_reset) {
			rc = OPAL_UNSUPPORTED;
			break;
		}

		/* We need do nothing on deassert time */
		if (assert_state != OPAL_ASSERT_RESET)
			break;

		rc = phb->ops->fundamental_reset(phb);
		if (rc < 0)
			prerror("PHB#%d: Failure on fundamental reset, rc=%lld\n",
				phb->opal_id, rc);
		break;
	case OPAL_PCI_HOT_RESET:
		if (!phb->ops->hot_reset) {
			rc = OPAL_UNSUPPORTED;
			break;
		}

		/* We need do nothing on deassert time */
		if (assert_state != OPAL_ASSERT_RESET)
			break;

		rc = phb->ops->hot_reset(phb);
		if (rc < 0)
			prerror("PHB#%d: Failure on hot reset, rc=%lld\n",
				phb->opal_id, rc);
		break;
	case OPAL_PCI_IODA_TABLE_RESET:
		if (assert_state != OPAL_ASSERT_RESET)
			break;
		if (phb->ops->ioda_reset)
			phb->ops->ioda_reset(phb, true);
		break;
	default:
		rc = OPAL_UNSUPPORTED;
	}
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return (rc > 0) ? tb_to_msecs(rc) : rc;
}
opal_call(OPAL_PCI_RESET, opal_pci_reset);

static int64_t opal_pci_poll(uint64_t phb_id)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops || !phb->ops->poll)
		return OPAL_UNSUPPORTED;

	phb->ops->lock(phb);
	rc = phb->ops->poll(phb);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_POLL, opal_pci_poll);

static int64_t opal_pci_set_phb_tce_memory(uint64_t phb_id,
					   uint64_t tce_mem_addr,
					   uint64_t tce_mem_size)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->set_phb_tce_memory)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->set_phb_tce_memory(phb, tce_mem_addr, tce_mem_size);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_SET_PHB_TCE_MEMORY, opal_pci_set_phb_tce_memory);

static int64_t opal_pci_get_phb_diag_data(uint64_t phb_id,
					  void *diag_buffer,
					  uint64_t diag_buffer_len)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->get_diag_data)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->get_diag_data(phb, diag_buffer, diag_buffer_len);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_GET_PHB_DIAG_DATA, opal_pci_get_phb_diag_data);

static int64_t opal_pci_next_error(uint64_t phb_id, uint64_t *first_frozen_pe,
				   uint16_t *pci_error_type, uint16_t *severity)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->next_error)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);

	/* Any call to this function clears the error event */
	opal_update_pending_evt(OPAL_EVENT_PCI_ERROR, 0);
	rc = phb->ops->next_error(phb, first_frozen_pe, pci_error_type,
				  severity);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_NEXT_ERROR, opal_pci_next_error);

static int64_t opal_pci_eeh_freeze_status2(uint64_t phb_id, uint64_t pe_number,
					   uint8_t *freeze_state,
					   uint16_t *pci_error_type,
					   uint16_t *severity,
					   uint64_t *phb_status)
{
	struct phb *phb = pci_get_phb(phb_id);
	int64_t rc;

	if (!phb)
		return OPAL_PARAMETER;
	if (!phb->ops->eeh_freeze_status)
		return OPAL_UNSUPPORTED;
	phb->ops->lock(phb);
	rc = phb->ops->eeh_freeze_status(phb, pe_number, freeze_state,
					 pci_error_type, severity, phb_status);
	phb->ops->unlock(phb);
	pci_put_phb(phb);

	return rc;
}
opal_call(OPAL_PCI_EEH_FREEZE_STATUS2, opal_pci_eeh_freeze_status2);


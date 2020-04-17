/**
 * @file main.c
 *
 * \brief Main of dummy
 *
 */

#include "libc/syscall.h"
#include "libc/stdio.h"
#include "libc/nostd.h"
#include "libc/string.h"
#include "wookey_ipc.h"
#include "usb.h"
#include "usb_control.h"
#include "scsi.h"
#include "libc/malloc.h"

#define USB_APP_DEBUG 1
#define USB_BUF_SIZE 16384

volatile bool reset_requested = false;
uint8_t id_crypto = 0;
static void my_irq_handler(void);

/* NOTE: alignment for DMA */
__attribute__ ((aligned(4)))
     uint8_t usb_buf[USB_BUF_SIZE] = { 0 };


void scsi_reset_device(void)
{
    reset_requested = true;
    scsi_reinit();
    reset_requested = false;
}


mbed_error_t storage_read(uint32_t sector_address, uint32_t num_sectors)
{

    struct dataplane_command dataplane_command_rd = { 0 };
    struct dataplane_command dataplane_command_ack = { 0 };
    uint8_t sinker = id_crypto;
    logsize_t ipcsize = sizeof(struct dataplane_command);
    uint8_t ret;

    dataplane_command_rd.magic = MAGIC_DATA_RD_DMA_REQ;
    dataplane_command_rd.sector_address = sector_address;
    dataplane_command_rd.num_sectors = num_sectors;

    /* ipc_dma_request to cryp */
    sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dataplane_command),
            (const char *) &dataplane_command_rd);

    sinker = id_crypto;
    ipcsize = sizeof(struct dataplane_command);
    ret = sys_ipc(IPC_RECV_SYNC, &sinker, &ipcsize,
                  (char *) &dataplane_command_ack);
    if (ret != SYS_E_DONE) {
        return MBED_ERROR_NONE;
    }

    if (dataplane_command_ack.magic != MAGIC_DATA_RD_DMA_ACK) {
        printf("dma request to sinker didn't received acknowledge\n");
        return MBED_ERROR_NOSTORAGE;
    }
#if USB_APP_DEBUG
    printf("==> storage_read10_data 0x%x %d\n",
           dataplane_command_rd.sector_address, num_sectors);
#endif
    return MBED_ERROR_NONE;
}


mbed_error_t storage_write(uint32_t sector_address, uint32_t num_sectors)
{
    struct dataplane_command dataplane_command_wr = { 0 };
    struct dataplane_command dataplane_command_ack = { 0 };
    uint8_t sinker = id_crypto;
    logsize_t ipcsize = sizeof(struct dataplane_command);
    uint8_t ret;

    dataplane_command_wr.magic = MAGIC_DATA_WR_DMA_REQ;
    dataplane_command_wr.sector_address = sector_address;
    dataplane_command_wr.num_sectors = num_sectors;

    /* ipc_dma_request to cryp */
    ret = sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dataplane_command),
                  (const char *) &dataplane_command_wr);
    if (ret != SYS_E_DONE) {
        return MBED_ERROR_NONE;
    }

    sinker = id_crypto;
    ipcsize = sizeof(struct dataplane_command);
    ret = sys_ipc(IPC_RECV_SYNC, &sinker, &ipcsize,
                  (char *) &dataplane_command_ack);
    if (ret != SYS_E_DONE) {
        return MBED_ERROR_NONE;
    }

    if (dataplane_command_ack.magic != MAGIC_DATA_WR_DMA_ACK) {
        printf("dma request to sinker didn't received acknowledge\n");
        return MBED_ERROR_NOSTORAGE;
    }
#if USB_APP_DEBUG
    printf("==> storage_write10_data 0x%x %d\n",
           dataplane_command_wr.sector_address, num_sectors);
#endif
    return MBED_ERROR_NONE;
}


void request_reboot(void)
{
    uint8_t ret;
    struct sync_command_data sync_command;

    sync_command.magic = MAGIC_REBOOT_REQUEST;
    sync_command.state = SYNC_WAIT;
    ret = sys_ipc(IPC_SEND_SYNC, id_crypto,
                  sizeof(struct sync_command), (char *) &sync_command);
    if (ret != SYS_E_DONE) {
        /* Request reboot failed ... This should not happen */
        while (1) {
        };
    }
}

/*
 * We use the local -fno-stack-protector flag for main because
 * the stack protection has not been initialized yet.
 */
int _main(uint32_t task_id)
{
    volatile e_syscall_ret ret = 0;
    uint8_t id;

    struct sync_command ipc_sync_cmd;
    struct sync_command_data ipc_sync_cmd_data;

    dma_shm_t dmashm_rd;
    dma_shm_t dmashm_wr;

    printf("Hello ! I'm usb, my id is %x\n", task_id);

    ret = sys_init(INIT_GETTASKID, "crypto", &id_crypto);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        goto error;
    }

    printf("crypto is task %x !\n", id_crypto);

    /*********************************************
     * Declaring DMA Shared Memory with Crypto
     *********************************************/
    dmashm_rd.target = id_crypto;
    dmashm_rd.source = task_id;
    dmashm_rd.address = (physaddr_t) usb_buf;
    dmashm_rd.size = USB_BUF_SIZE;
    /* Crypto DMA will read from this buffer */
    dmashm_rd.mode = DMA_SHM_ACCESS_RD;

    dmashm_wr.target = id_crypto;
    dmashm_wr.source = task_id;
    dmashm_wr.address = (physaddr_t) usb_buf;
    dmashm_wr.size = USB_BUF_SIZE;
    /* Crypto DMA will write into this buffer */
    dmashm_wr.mode = DMA_SHM_ACCESS_WR;

    printf("Declaring DMA_SHM for SDIO read flow\n");

    ret = sys_init(INIT_DMA_SHM, &dmashm_rd);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        goto error;
    }

    printf("sys_init returns %s !\n", strerror(ret));

    printf("Declaring DMA_SHM for SDIO write flow\n");

    ret = sys_init(INIT_DMA_SHM, &dmashm_wr);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        goto error;
    }

    printf("sys_init returns %s !\n", strerror(ret));

    if (scsi_early_init(usb_buf, USB_BUF_SIZE)) {
        printf("ERROR: Unable to early initialize SCSI stack! leaving...\n");
        goto error;
    }

    /*******************************************
     * End of init
     *******************************************/

    ret = sys_init(INIT_DONE);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        goto error;
    }

    printf("sys_init DONE returns %x !\n", ret);

    /*******************************************
     * Let's syncrhonize with other tasks
     *******************************************/
    logsize_t size = sizeof(struct sync_command);

    printf("sending end_of_init synchronization to crypto\n");
    ipc_sync_cmd.magic = MAGIC_TASK_STATE_CMD;
    ipc_sync_cmd.state = SYNC_READY;

    ret = sys_ipc(IPC_SEND_SYNC, id_crypto, size, (const char *) &ipc_sync_cmd);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        goto error;
    }

    printf("end of end_of_init synchro.\n");

    /* Now wait for Acknowledge from Smart */
    id = id_crypto;
    ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char *) &ipc_sync_cmd);
    if (ret != SYS_E_DONE) {
        printf("ack from crypto: Oops ! ret = %d\n", ret);
        goto error;
    } else {
        printf("Acknowledge from crypto ok\n");
    }

    if ((ipc_sync_cmd.magic == MAGIC_TASK_STATE_RESP)
        && (ipc_sync_cmd.state == SYNC_ACKNOWLEDGE)) {
        printf("crypto has acknowledge end_of_init, continuing\n");
    }

    /*******************************************
     * Starting end_of_cryp synchronization
     *******************************************/

    printf("waiting end_of_cryp syncrhonization from crypto\n");

    id = id_crypto;
    size = sizeof(struct sync_command);

    ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char *) &ipc_sync_cmd);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        goto error;
    }

    if ((ipc_sync_cmd.magic == MAGIC_TASK_STATE_CMD)
        && (ipc_sync_cmd.state == SYNC_READY)) {
        printf("crypto module is ready\n");
    }

    /* Initialize USB device */

    /************************************************
     * Sending crypto end_of_service_init
     ***********************************************/

    ipc_sync_cmd.magic = MAGIC_TASK_STATE_RESP;
    ipc_sync_cmd.state = SYNC_READY;

    size = sizeof(struct sync_command);
    ret = sys_ipc(IPC_SEND_SYNC, id_crypto, size, (char *) &ipc_sync_cmd);
    if (ret != SYS_E_DONE) {
        printf("sending end of services init to crypto: Oops ! ret = %d\n",
               ret);
        goto error;
    }

    printf("sending end of services init to crypto ok\n");

    /* waiting for crypto acknowledge */
    ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char *) &ipc_sync_cmd);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        goto error;
    }

    if ((ipc_sync_cmd.magic == MAGIC_TASK_STATE_RESP)
        && (ipc_sync_cmd.state == SYNC_ACKNOWLEDGE)) {
        printf("crypto has acknowledge sync ready, continuing\n");
    } else {
        printf("Error ! IPC desynchro !\n");
        goto error;
    }


    /*******************************************
     * Sharing DMA SHM address and size with crypto
     *******************************************/
    ipc_sync_cmd_data.magic = MAGIC_DMA_SHM_INFO_CMD;
    ipc_sync_cmd_data.state = SYNC_READY;
    ipc_sync_cmd_data.data_size = 2;
    ipc_sync_cmd_data.data.u32[0] = (uint32_t) usb_buf;
    ipc_sync_cmd_data.data.u32[1] = USB_BUF_SIZE;

    printf("informing crypto about DMA SHM...\n");

    ret = sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct sync_command_data),
                  (char *) &ipc_sync_cmd_data);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        goto error;
    }

    printf("Crypto informed.\n");

    ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char *) &ipc_sync_cmd);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        goto error;
    }

    if ((ipc_sync_cmd.magic == MAGIC_DMA_SHM_INFO_RESP)
        && (ipc_sync_cmd.state == SYNC_ACKNOWLEDGE)) {
        printf("crypto has acknowledge DMA SHM, continuing\n");
    } else {
        printf("Error ! IPC desynchro !\n");
        goto error;
    }

    /*******************************************
     * End of init sequence, let's initialize devices
     *******************************************/

    scsi_init();

    /*******************************************
     * Starting USB listener
     *******************************************/

    printf("USB main loop starting\n");

    while (1) {
        scsi_exec_automaton();
        aprintf_flush();
    }

 error:
    printf("Going to error state!\n");
    aprintf_flush();
    return 1;
}

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

uint8_t id_crypto = 0;

volatile bool reset_requested = false;

void usb_do_reset(void)
{
    e_syscall_ret ret;

    struct sync_command ipc_sync_cmd;

    memset((void *) &ipc_sync_cmd, 0, sizeof(struct sync_command));

    ipc_sync_cmd.magic = MAGIC_REBOOT_REQUEST;
    ret =
        sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct sync_command),
                (char *) &ipc_sync_cmd);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("%s:%d Oops ! ret = %d\n", __func__, __LINE__, ret);
#endif
    }
    while (1) {
        /* voluntary freeze, in our case, as this reset order request
         * reboot */
        continue;
    }
    return;
}


void scsi_reset_device(void)
{
    reset_requested = true;
}



mbed_error_t storage_read(uint32_t sector_address, uint32_t num_sectors)
{

    struct dataplane_command dataplane_command_rd = { 0 };
    struct dataplane_command dataplane_command_ack = { 0 };
    uint8_t sinker = id_crypto;
    logsize_t ipcsize = sizeof(struct dataplane_command);

    dataplane_command_rd.magic = MAGIC_DATA_RD_DMA_REQ;
    dataplane_command_rd.sector_address = sector_address;
    dataplane_command_rd.num_sectors = num_sectors;
    // ipc_dma_request to cryp
    sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dataplane_command),
            (const char *) &dataplane_command_rd);
    ipcsize = sizeof(struct dataplane_command);
    sys_ipc(IPC_RECV_SYNC, &sinker, &ipcsize, (char *) &dataplane_command_ack);
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

    dataplane_command_wr.magic = MAGIC_DATA_WR_DMA_REQ;
    dataplane_command_wr.sector_address = sector_address;
    dataplane_command_wr.num_sectors = num_sectors;
    // ipc_dma_request to cryp
    sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dataplane_command),
            (const char *) &dataplane_command_wr);
    sinker = id_crypto;
    ipcsize = sizeof(struct dataplane_command);
    sys_ipc(IPC_RECV_SYNC, &sinker, &ipcsize, (char *) &dataplane_command_ack);
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



static void my_irq_handler(void);

#define USB_BUF_SIZE 16384

/* NOTE: alignment for DMA */
__attribute__((aligned(4)))
     uint8_t usb_buf[USB_BUF_SIZE] = { 0 };

void request_reboot(void){
        struct sync_command_data sync_command;
        sync_command.magic = MAGIC_REBOOT_REQUEST;
        sync_command.state = SYNC_WAIT;
        sys_ipc(IPC_SEND_SYNC, id_crypto,
                    sizeof(struct sync_command),
                    (char*)&sync_command);
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
    printf("sys_init returns %s !\n", strerror(ret));

    printf("Declaring DMA_SHM for SDIO write flow\n");
    ret = sys_init(INIT_DMA_SHM, &dmashm_wr);
    printf("sys_init returns %s !\n", strerror(ret));


    if (scsi_early_init(usb_buf, USB_BUF_SIZE)) {
        printf("ERROR: Unable to early initialize SCSI stack! leaving...\n");
        goto err;
    }

    /*******************************************
     * End of init
     *******************************************/

    ret = sys_init(INIT_DONE);
    printf("sys_init DONE returns %x !\n", ret);


    /*******************************************
     * let's syncrhonize with other tasks
     *******************************************/
    logsize_t size = sizeof(struct sync_command);

    printf("sending end_of_init synchronization to crypto\n");
    ipc_sync_cmd.magic = MAGIC_TASK_STATE_CMD;
    ipc_sync_cmd.state = SYNC_READY;

    do {
        ret = 42;
        ret =
            sys_ipc(IPC_SEND_SYNC, id_crypto, size,
                    (const char *) &ipc_sync_cmd);
        if (ret != SYS_E_DONE) {
            printf("Oops ! ret = %d\n", ret);
        } else {
            printf("end of end_of_init synchro.\n");
        }
    } while (ret != SYS_E_DONE);

    /* Now wait for Acknowledge from Smart */
    id = id_crypto;

    do {
        ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char *) &ipc_sync_cmd);
        if (ret != SYS_E_DONE) {
            printf("ack from crypto: Oops ! ret = %d\n", ret);
        } else {
            printf("Acknowledge from crypto ok\n");
        }
    } while (ret != SYS_E_DONE);
    if (ipc_sync_cmd.magic == MAGIC_TASK_STATE_RESP
        && ipc_sync_cmd.state == SYNC_ACKNOWLEDGE) {
        printf("crypto has acknowledge end_of_init, continuing\n");
    }

    /*******************************************
     * Starting end_of_cryp synchronization
     *******************************************/

    printf("waiting end_of_cryp syncrhonization from crypto\n");

    id = id_crypto;
    size = sizeof(struct sync_command);

    do {
        ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char *) &ipc_sync_cmd);
    } while (ret == SYS_E_BUSY);

    if (ipc_sync_cmd.magic == MAGIC_TASK_STATE_CMD
        && ipc_sync_cmd.state == SYNC_READY) {
        printf("crypto module is ready\n");
    }

    /* Initialize USB device */
    wmalloc_init();

    /************************************************
     * Sending crypto end_of_service_init
     ***********************************************/

    ipc_sync_cmd.magic = MAGIC_TASK_STATE_RESP;
    ipc_sync_cmd.state = SYNC_READY;

    size = sizeof(struct sync_command);
    do {
        ret = sys_ipc(IPC_SEND_SYNC, id_crypto, size, (char *) &ipc_sync_cmd);
        if (ret != SYS_E_DONE) {
            printf("sending end of services init to crypto: Oops ! ret = %d\n",
                   ret);
        } else {
            printf("sending end of services init to crypto ok\n");
        }
    } while (ret == SYS_E_BUSY);

    /* waiting for crypto acknowledge */
    ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char *) &ipc_sync_cmd);
    if (ipc_sync_cmd.magic == MAGIC_TASK_STATE_RESP
        && ipc_sync_cmd.state == SYNC_ACKNOWLEDGE) {
        printf("crypto has acknowledge sync ready, continuing\n");
    } else {
        printf("Error ! IPC desynchro !\n");
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
    do {
        ret =
            sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct sync_command_data),
                    (char *) &ipc_sync_cmd_data);
    } while (ret == SYS_E_BUSY);
    printf("Crypto informed.\n");

    ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char *) &ipc_sync_cmd);
    if (ipc_sync_cmd.magic == MAGIC_DMA_SHM_INFO_RESP
        && ipc_sync_cmd.state == SYNC_ACKNOWLEDGE) {
        printf("crypto has acknowledge DMA SHM, continuing\n");
    } else {
        printf("Error ! IPC desynchro !\n");
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
        if (reset_requested) {
            usb_do_reset();
        }
        scsi_exec_automaton();
        aprintf_flush();
        /* Check if an USB reset request has been performed by the host ... */
        if(usb_is_reset_asked()){
            printf("USB reset asked by the host ...\n");
            request_reboot();
        }	
    }


 err:
    printf("Going to error state!\n");
    while (1) {
        sys_yield();
        aprintf_flush();
    }
    /* should return to do_endoftask() */
    return 0;
}

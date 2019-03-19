/**
 * @file main.c
 *
 * \brief Main of dummy
 *
 */

#include "api/syscall.h"
#include "api/stdio.h"
#include "api/nostd.h"
#include "api/string.h"
#include "wookey_ipc.h"
#include "usb.h"
#include "usb_control.h"
#include "scsi.h"
#include "masstorage.h"
#include "api/malloc.h"

#define USB_APP_DEBUG 1

uint8_t id_crypto = 0;


uint8_t storage_read(uint32_t sector_address,
                  uint32_t num_sectors)
{

    struct dataplane_command dataplane_command_rd = { 0 };
    struct dataplane_command dataplane_command_ack = { 0 };
    uint8_t sinker = id_crypto;
    logsize_t ipcsize = sizeof(struct dataplane_command);

    dataplane_command_rd.magic = MAGIC_DATA_RD_DMA_REQ;
    dataplane_command_rd.sector_address = sector_address;
    dataplane_command_rd.num_sectors = num_sectors;
    // ipc_dma_request to cryp
    sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dataplane_command), (const char*)&dataplane_command_rd);
    sinker = id_crypto;
    ipcsize = sizeof(struct dataplane_command);
    sys_ipc(IPC_RECV_SYNC, &sinker, &ipcsize, (char*)&dataplane_command_ack);
    if (dataplane_command_ack.magic != MAGIC_DATA_RD_DMA_ACK) {
        printf("dma request to sinker didn't received acknowledge\n");
        return -1;
    }
    #if USB_APP_DEBUG
        printf("==> storage_read10_data 0x%x %d\n", dataplane_command_rd.sector_address, num_sectors);
    #endif
    return 0;
}

uint8_t storage_write(uint32_t sector_address,
                   uint32_t num_sectors)
{
    struct dataplane_command dataplane_command_wr = { 0 };
    struct dataplane_command dataplane_command_ack = { 0 };
    uint8_t sinker = id_crypto;
    logsize_t ipcsize = sizeof(struct dataplane_command);

    dataplane_command_wr.magic = MAGIC_DATA_WR_DMA_REQ;
    dataplane_command_wr.sector_address = sector_address;
    dataplane_command_wr.num_sectors = num_sectors;
    // ipc_dma_request to cryp
    sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dataplane_command), (const char*)&dataplane_command_wr);
    sinker = id_crypto;
    ipcsize = sizeof(struct dataplane_command);
    sys_ipc(IPC_RECV_SYNC, &sinker, &ipcsize, (char*)&dataplane_command_ack);
    if (dataplane_command_ack.magic != MAGIC_DATA_WR_DMA_ACK) {
        printf("dma request to sinker didn't received acknowledge\n");
        return -1;
    }

    #if USB_APP_DEBUG
        printf("==> storage_write10_data 0x%x %d\n", dataplane_command_wr.sector_address, num_sectors);
    #endif
    return 0;
}



static uint8_t get_storage_capacity(uint32_t * storage_capacity){

    #if USB_APP_DEBUG
        printf("%s\n", __func__);
    #endif

    logsize_t size = sizeof(struct sync_command_data);
    e_syscall_ret ret;

    uint8_t sinker = id_crypto;

    struct sync_command_data ipc_sync_cmd_data;
    memset((void*)&ipc_sync_cmd_data, 0, sizeof(struct sync_command_data));

    ipc_sync_cmd_data.magic = MAGIC_STORAGE_SCSI_BLOCK_NUM_CMD;
    do {
        ret = 42;
        ret = sys_ipc(IPC_SEND_SYNC, sinker, sizeof(struct sync_command), (char*)&ipc_sync_cmd_data);
        if (ret != SYS_E_DONE) {
            # if USB_APP_DEBUG
                printf("%s:%d Oops ! ret = %d\n", __func__, __LINE__, ret);
            #endif
        } else {
            # if USB_APP_DEBUG
                printf("%s:%d IPC MAGIC_STORAGE_SCSI_BLOCK_SIZE_CMD succesfully sent.\n", __func__, __LINE__);
            #endif
        }
    } while (ret != SYS_E_DONE);


    do {
        ret = 42;
        ret = sys_ipc(IPC_RECV_SYNC, &sinker, &size, (char*)&ipc_sync_cmd_data);
        if (ret != SYS_E_DONE) {
        # if USB_APP_DEBUG
                printf("%s:%d Oops ! ret = %d\n", __func__, __LINE__, ret);
        #endif
        } else {
        # if USB_APP_DEBUG
            printf("%s:%d Got IPC_RECV_SYNC.\n");
        #endif

        }
    } while (ret != SYS_E_DONE);

    if (ipc_sync_cmd_data.magic == MAGIC_STORAGE_SCSI_BLOCK_NUM_RESP){
        //block_size = ipc_sync_cmd_data.data.u32[0];
        *storage_capacity = ipc_sync_cmd_data.data.u32[1];
        # if USB_APP_DEBUG
            printf("%s:%d Received storage_capacity is %d\n",  __func__, __LINE__, *storage_capacity);
        #endif
        return 0;
    }
    #if USB_APP_DEBUG
        printf("%s:%d ERROR: getting capacity from lower layers ...\n",  __func__, __LINE__);
    #endif
    return -1;

}


static uint8_t  get_storage_block_size(uint32_t * block_size){
#if USB_APP_DEBUG
    printf("%s\n", __func__);
#endif

    logsize_t size = sizeof(struct sync_command_data);
    e_syscall_ret ret;
    uint8_t sinker = id_crypto;

    struct sync_command_data ipc_sync_cmd_data;
    memset((void*)&ipc_sync_cmd_data, 0, sizeof(struct sync_command_data));

    ipc_sync_cmd_data.magic = MAGIC_STORAGE_SCSI_BLOCK_SIZE_CMD;

    do {

        do {
            ret = 42;
            ret = sys_ipc(IPC_SEND_SYNC, sinker, sizeof(struct sync_command), (char*)&ipc_sync_cmd_data);
            if (ret != SYS_E_DONE) {
# if USB_APP_DEBUG
                printf("%s:%d Oops ! ret = %d\n",  __func__, __LINE__, ret);
#endif
            } else {
# if USB_APP_DEBUG
                printf("%s:%d IPC MAGIC_STORAGE_SCSI_BLOCK_SIZE_CMD succesfully sent.\n", __func__, __LINE__);
#endif
            }
        } while(ret!== SYS_E_DONE);

        do {
            if (ret == SYS_E_DONE) {
                ret = 42;
                ret = sys_ipc(IPC_RECV_SYNC, &sinker, &size, (char*)&ipc_sync_cmd_data);
                if (ret != SYS_E_DONE) {
# if USB_APP_DEBUG
                    printf("%s:%d Oops ! ret = %d\n",  __func__, __LINE__, ret);
#endif
                } else {
# if USB_APP_DEBUG
                    printf("%s:%d Got IPC_RECV_SYNC.\n", __func__, __LINE__);
#endif
                }
                if (ret != SYS_E_DONE) {
                    goto error;
                }
            }
        } while(ret!== SYS_E_DONE);


    } while (ipc_sync_cmd_data.magic != MAGIC_STORAGE_SCSI_BLOCK_SIZE_RESP);


    *block_size = ipc_sync_cmd_data.data.u32[0];
    # if USB_APP_DEBUG
        printf("%s:%d Received block size is %d\n", __func__,  __LINE__, *block_size);
    #endif
    return 0;

error:
    # if USB_APP_DEBUG
        printf("%s:%d ERROR: getting block size from lower layers ...\n", __func__,  __LINE__);
    #endif
    return -1;
}




static void my_irq_handler(void);

#define USB_BUF_SIZE 16384

/* NOTE: alignment for DMA */
__attribute__((aligned(4))) uint8_t usb_buf[USB_BUF_SIZE] = { 0 };

/*
 * We use the local -fno-stack-protector flag for main because
 * the stack protection has not been initialized yet.
 */
int _main(uint32_t task_id)
{
    volatile e_syscall_ret ret = 0;
    uint8_t id;

    struct sync_command      ipc_sync_cmd;

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
    dmashm_rd.address = (physaddr_t)usb_buf;
    dmashm_rd.size = USB_BUF_SIZE;
    /* Crypto DMA will read from this buffer */
    dmashm_rd.mode = DMA_SHM_ACCESS_RD;

    dmashm_wr.target = id_crypto;
    dmashm_wr.source = task_id;
    dmashm_wr.address = (physaddr_t)usb_buf;
    dmashm_wr.size = USB_BUF_SIZE;
    /* Crypto DMA will write into this buffer */
    dmashm_wr.mode = DMA_SHM_ACCESS_WR;

    printf("Declaring DMA_SHM for SDIO read flow\n");
    ret = sys_init(INIT_DMA_SHM, &dmashm_rd);
    printf("sys_init returns %s !\n", strerror(ret));

    printf("Declaring DMA_SHM for SDIO write flow\n");
    ret = sys_init(INIT_DMA_SHM, &dmashm_wr);
    printf("sys_init returns %s !\n", strerror(ret));

    /* initialize the SCSI stack with two buffers of 4096 bits length each. */
    scsi_calbacks_t scsi_cb = {
        .read = storage_read,
        .write = storage_write,
    };


    if (scsi_early_init(usb_buf, USB_BUF_SIZE, &scsi_cb)) {
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
    logsize_t size = sizeof (struct sync_command);

    printf("sending end_of_init synchronization to crypto\n");
    ipc_sync_cmd.magic = MAGIC_TASK_STATE_CMD;
    ipc_sync_cmd.state = SYNC_READY;

    do {
      ret = 42;
      ret = sys_ipc(IPC_SEND_SYNC, id_crypto, size, (const char*)&ipc_sync_cmd);
      if (ret != SYS_E_DONE) {
          printf("Oops ! ret = %d\n", ret);
      } else {
          printf("end of end_of_init synchro.\n");
      }
    } while (ret != SYS_E_DONE);

    /* Now wait for Acknowledge from Smart */
    id = id_crypto;

    do {
        ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char*)&ipc_sync_cmd);
      if (ret != SYS_E_DONE) {
          printf("ack from crypto: Oops ! ret = %d\n", ret);
      } else {
          printf("Acknowledge from crypto ok\n");
      }
    } while (ret != SYS_E_DONE);
    if (   ipc_sync_cmd.magic == MAGIC_TASK_STATE_RESP
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
        ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char*)&ipc_sync_cmd);
    } while (ret == SYS_E_BUSY);

    if (   ipc_sync_cmd.magic == MAGIC_TASK_STATE_CMD
        && ipc_sync_cmd.state == SYNC_READY) {
        printf("crypto module is ready\n");
    }

    /* Initialize USB device */
    wmalloc_init();
    ipc_sync_cmd.magic = MAGIC_TASK_STATE_RESP;
    ipc_sync_cmd.state = SYNC_READY;

    size = sizeof(struct sync_command);
    do {
      ret = sys_ipc(IPC_SEND_SYNC, id_crypto, size, (char*)&ipc_sync_cmd);
      if (ret != SYS_E_DONE) {
          printf("sending Sync ready to crypto: Oops ! ret = %d\n", ret);
      } else {
          printf("sending sync ready to crypto ok\n");
      }
    } while (ret == SYS_E_BUSY);

    // take some time to finish all sync ipc...
    sys_sleep(1000, SLEEP_MODE_INTERRUPTIBLE);

    /*******************************************
     * Sharing DMA SHM address and size with crypto
     *******************************************/
    struct dmashm_info {
        uint32_t addr;
        uint16_t size;
    };
    struct dmashm_info dmashm_info;

    dmashm_info.addr = (uint32_t)usb_buf;
    dmashm_info.size = USB_BUF_SIZE;

    printf("informing crypto about DMA SHM...\n");
    do {
      ret = sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dmashm_info), (char*)&dmashm_info);
    } while (ret == SYS_E_BUSY);
    printf("Crypto informed.\n");

    /*******************************************
     * End of init sequence, let's initialize devices
     *******************************************/

    // take some time to finish all sync ipc...
    //sys_sleep(1000, SLEEP_MODE_INTERRUPTIBLE);

    uint32_t block_size = 0;
    ret = get_storage_block_size(&block_size);
    if ( ret != 0){
        printf("%s: ERROR Getting storage block size\n", __func__);
        while(1) {};
    }
    printf("%s: storage_block_size: %d\n", __func__, block_size);


    uint32_t storage_size = 0;
	ret = get_storage_capacity(&storage_size);
    if (ret != 0) {
        printf("%s: ERROR Getting storage capacity\n", __func__);
        while(1) {};
    }
    printf("%s: storage_size: %d\n", __func__, storage_size);


    scsi_init(storage_size, block_size);
    mass_storage_init();


    /*******************************************
     * Starting USB listener
     *******************************************/

    printf("USB main loop starting\n");

err:
    while (1) {
        scsi_exec_automaton();
        sys_yield();
        aprintf_flush();
    }
    /* should return to do_endoftask() */
    return 0;
}

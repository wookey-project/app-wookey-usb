#include "api/syscall.h"
#include "api/stdio.h"
#include "api/nostd.h"
#include "api/string.h"
#include "wookey_ipc.h"
#include "scsi.h"

#define USB_APP_DEBUG 1

extern uint8_t id_crypto;


uint8_t scsi_storage_backend_capacity(uint32_t *numblocks, uint32_t *blocksize)
{

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

    if (ipc_sync_cmd_data.magic == MAGIC_STORAGE_SCSI_BLOCK_NUM_RESP) {
        //block_size = ipc_sync_cmd_data.data.u32[0];
        *numblocks = ipc_sync_cmd_data.data.u32[1];
        # if USB_APP_DEBUG
            printf("%s:%d Received storage_capacity is %d\n",  __func__, __LINE__, *numblocks);
        #endif
    }

    memset((void*)&ipc_sync_cmd_data, 0, sizeof(struct sync_command_data));
    ipc_sync_cmd_data.magic = MAGIC_STORAGE_SCSI_BLOCK_SIZE_CMD;

    do {

        do {
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
        } while(ret!= SYS_E_DONE);

        do {
            if (ret == SYS_E_DONE) {
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
        } while(ret!= SYS_E_DONE);

    } while (ipc_sync_cmd_data.magic != MAGIC_STORAGE_SCSI_BLOCK_SIZE_RESP);


    *blocksize = ipc_sync_cmd_data.data.u32[0];
    # if USB_APP_DEBUG
        printf("%s:%d Received block size is %d\n", __func__,  __LINE__, *blocksize);
    #endif
    return 0;

error:
    # if USB_APP_DEBUG
        printf("%s:%d ERROR: getting block size from lower layers ...\n", __func__,  __LINE__);
    #endif
    return 1;

}


uint8_t scsi_storage_backend_read(uint32_t sector_address,
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
        return 1;
    }
    #if USB_APP_DEBUG
        printf("==> storage_read10_data 0x%x %d\n", dataplane_command_rd.sector_address, num_sectors);
    #endif
    return 0;
}

uint8_t scsi_storage_backend_write(uint32_t sector_address,
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
#if USB_APP_DEBUG
    printf("requesting %x num sectors, addr 0x%08x\n", num_sectors, sector_address);
#endif
    sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dataplane_command), (const char*)&dataplane_command_wr);
    sinker = id_crypto;
    ipcsize = sizeof(struct dataplane_command);
    sys_ipc(IPC_RECV_SYNC, &sinker, &ipcsize, (char*)&dataplane_command_ack);
    if (dataplane_command_ack.magic != MAGIC_DATA_WR_DMA_ACK) {
        printf("dma request to sinker didn't received acknowledge\n");
        return 1;
    }

    #if USB_APP_DEBUG
        printf("==> storage_write10_data 0x%x %d\n", dataplane_command_wr.sector_address, num_sectors);
    #endif
    return 0;
}




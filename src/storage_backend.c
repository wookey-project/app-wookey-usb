#include "libc/syscall.h"
#include "libc/stdio.h"
#include "libc/nostd.h"
#include "libc/string.h"
#include "wookey_ipc.h"
#include "scsi.h"

#define USB_APP_DEBUG 0

extern uint8_t id_crypto;

mbed_error_t scsi_storage_backend_capacity(uint32_t * numblocks,
                                           uint32_t * blocksize)
{
    mbed_error_t error_code = MBED_ERROR_NONE;
    logsize_t size = sizeof(struct sync_command_data);
    uint8_t sinker = id_crypto;
    struct sync_command_data ipc_sync_cmd_data;
    e_syscall_ret ret;

#if USB_APP_DEBUG
    printf("%s\n", __func__);
#endif

    memset((void *) &ipc_sync_cmd_data, 0, sizeof(struct sync_command_data));

    ipc_sync_cmd_data.magic = MAGIC_STORAGE_SCSI_BLOCK_NUM_CMD;
    ret =
        sys_ipc(IPC_SEND_SYNC, sinker, sizeof(struct sync_command),
                (char *) &ipc_sync_cmd_data);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    }
#if USB_APP_DEBUG
    printf("%s:%d IPC MAGIC_STORAGE_SCSI_BLOCK_SIZE_CMD succesfully sent.\n",
           __func__, __LINE__);
#endif

    ret = sys_ipc(IPC_RECV_SYNC, &sinker, &size, (char *) &ipc_sync_cmd_data);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    }
#if USB_APP_DEBUG
    printf("%s:%d Got IPC_RECV_SYNC.\n");
#endif

    if (ipc_sync_cmd_data.magic == MAGIC_STORAGE_SCSI_BLOCK_NUM_RESP) {
        *numblocks = ipc_sync_cmd_data.data.u32[1];
#if USB_APP_DEBUG
        printf("%s:%d Received storage_capacity is %d\n", __func__, __LINE__,
               *numblocks);
#endif
    } else {
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    }

    memset((void *) &ipc_sync_cmd_data, 0, sizeof(struct sync_command_data));
    ipc_sync_cmd_data.magic = MAGIC_STORAGE_SCSI_BLOCK_SIZE_CMD;

    ret =
        sys_ipc(IPC_SEND_SYNC, sinker, sizeof(struct sync_command),
                (char *) &ipc_sync_cmd_data);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    } else {
#if USB_APP_DEBUG
        printf
            ("%s:%d IPC MAGIC_STORAGE_SCSI_BLOCK_SIZE_CMD succesfully sent.\n",
             __func__, __LINE__);
#endif
    }

    ret = sys_ipc(IPC_RECV_SYNC, &sinker, &size, (char *) &ipc_sync_cmd_data);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("Oops! %s:%d\n", __func__, __LINE__);
#endif
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    } else {
#if USB_APP_DEBUG
        printf("%s:%d Got IPC_RECV_SYNC.\n", __func__, __LINE__);
#endif
    }

    *blocksize = ipc_sync_cmd_data.data.u32[0];
#if USB_APP_DEBUG
    printf("%s:%d Received block size is %d\n", __func__, __LINE__, *blocksize);
#endif
    return error_code;

 error:
#if USB_APP_DEBUG
    printf("%s:%d ERROR: getting block size from lower layers ...\n", __func__,
           __LINE__);
#endif
    return error_code;
}


mbed_error_t scsi_storage_backend_read(uint32_t sector_address,
                                       uint32_t num_sectors)
{
    struct dataplane_command dataplane_command_rd = { 0 };
    struct dataplane_command dataplane_command_ack = { 0 };
    uint8_t sinker = id_crypto;
    logsize_t ipcsize = sizeof(struct dataplane_command);
    mbed_error_t error_code = MBED_ERROR_NONE;
    uint8_t ret;

    dataplane_command_rd.magic = MAGIC_DATA_RD_DMA_REQ;
    dataplane_command_rd.sector_address = sector_address;
    dataplane_command_rd.num_sectors = num_sectors;

    /* ipc_dma_request to cryp */
    ret =
        sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dataplane_command),
                (const char *) &dataplane_command_rd);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("%s: fail to send IPC to sdio\n", __func__);
#endif
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    }

    sinker = id_crypto;
    ipcsize = sizeof(struct dataplane_command);
    ret =
        sys_ipc(IPC_RECV_SYNC, &sinker, &ipcsize,
                (char *) &dataplane_command_ack);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("%s: fail to receive IPC from sdio\n", __func__);
#endif
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    }

    if (dataplane_command_ack.magic != MAGIC_DATA_RD_DMA_ACK) {
#if USB_APP_DEBUG
        printf("dma request to sinker didn't received acknowledge\n");
#endif
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    }

    if (dataplane_command_ack.state != SYNC_ACKNOWLEDGE) {
        if (dataplane_command_ack.state == SYNC_FAILURE) {
#if USB_APP_DEBUG
            printf("%s: sdio said read error!\n", __func__);
#endif
            error_code = MBED_ERROR_RDERROR;
        } else {
#if USB_APP_DEBUG
            printf("%s: sdio state is unknown!\n", __func__);
#endif
            error_code = MBED_ERROR_UNKNOWN;
        }
    }
#if USB_APP_DEBUG
    printf("==> storage_read10_data 0x%x %d\n",
           dataplane_command_rd.sector_address, num_sectors);
#endif

 error:
    return error_code;
}


mbed_error_t scsi_storage_backend_write(uint32_t sector_address,
                                        uint32_t num_sectors)
{
    struct dataplane_command dataplane_command_wr = { 0 };
    struct dataplane_command dataplane_command_ack = { 0 };
    uint8_t sinker = id_crypto;
    logsize_t ipcsize = sizeof(struct dataplane_command);
    mbed_error_t error_code = MBED_ERROR_NONE;
    uint8_t ret;

    dataplane_command_wr.magic = MAGIC_DATA_WR_DMA_REQ;
    dataplane_command_wr.sector_address = sector_address;
    dataplane_command_wr.num_sectors = num_sectors;

    /* ipc_dma_request to cryp */
#if USB_APP_DEBUG
    printf("requesting %x num sectors, addr 0x%08x\n", num_sectors,
           sector_address);
#endif

    ret =
        sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dataplane_command),
                (const char *) &dataplane_command_wr);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("%s: fail to send IPC to sdio\n", __func__);
#endif
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    }

    sinker = id_crypto;
    ipcsize = sizeof(struct dataplane_command);
    ret =
        sys_ipc(IPC_RECV_SYNC, &sinker, &ipcsize,
                (char *) &dataplane_command_ack);
    if (ret != SYS_E_DONE) {
#if USB_APP_DEBUG
        printf("%s: fail to receive IPC from sdio\n", __func__);
#endif
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    }

    if (dataplane_command_ack.magic != MAGIC_DATA_WR_DMA_ACK) {
#if USB_APP_DEBUG
        printf("dma request to sinker didn't received acknowledge\n");
#endif
        error_code = MBED_ERROR_NOSTORAGE;
        goto error;
    }

    if (dataplane_command_ack.state != SYNC_ACKNOWLEDGE) {
        if (dataplane_command_ack.state == SYNC_FAILURE) {
#if USB_APP_DEBUG
            printf("%s: sdio said write error!\n", __func__);
#endif
            error_code = MBED_ERROR_WRERROR;
        } else {
#if USB_APP_DEBUG
            printf("%s: sdio said unknown error!\n", __func__);
#endif
            error_code = MBED_ERROR_UNKNOWN;
        }
    }
#if USB_APP_DEBUG
    printf("==> storage_write10_data 0x%x %d\n",
           dataplane_command_wr.sector_address, num_sectors);
#endif

 error:
    return error_code;
}

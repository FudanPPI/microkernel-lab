/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#include "mmc.h"
#include "services.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

#define DBG_INFO "info:"

//#define DEBUG
#undef DEBUG
#ifdef DEBUG
#define D(x, ...) printf(__VA_ARGS__)
#else
#define D(...) do{}while(0)
#endif

struct mmc_completion_token {
    struct mmc_card* card;
    mmc_cb cb;
    void* token;
};


static uint32_t slice_bits(uint32_t *val, int start, int size)
{
    int idx;
    int high, low;
    uint32_t ret = 0;

    /* Can not return more than 32 bits. */
    assert(size <= 32);

    idx = start / 32;
    low = start % 32;
    high = (start + size) % 32;

    if (high == 0 && low == 0) {
        ret = val[idx];
    } else if (high == 0 && low != 0) {
        ret = val[idx] >> low;
    } else {
        if (high > low) {
            ret = val[idx] & ((1U << high) - 1);
            ret = ret >> low;
        } else {
            ret = val[idx] >> low;
            ret |= (val[idx + 1] & ((1U << high) - 1)) << (32 - low);
        }

    }

    return ret;
}

#if 0 /* Commenting this out as it appears unused. */
static int mmc_decode_cid(mmc_card_t mmc_card, struct cid *cid)
{
    if (mmc_card == NULL || cid == NULL) {
        return -1;
    }

    if (mmc_card->type == CARD_TYPE_SD) {
        cid->manfid         = slice_bits(mmc_card->raw_cid, 120,  8);
        cid->sd_cid.oemid   = slice_bits(mmc_card->raw_cid, 104, 16);
        cid->sd_cid.name[0] = slice_bits(mmc_card->raw_cid,  96,  8);
        cid->sd_cid.name[1] = slice_bits(mmc_card->raw_cid,  88,  8);
        cid->sd_cid.name[2] = slice_bits(mmc_card->raw_cid,  80,  8);
        cid->sd_cid.name[3] = slice_bits(mmc_card->raw_cid,  72,  8);
        cid->sd_cid.name[4] = slice_bits(mmc_card->raw_cid,  64,  8);
        cid->sd_cid.rev     = slice_bits(mmc_card->raw_cid,  56,  8);
        cid->sd_cid.serial  = slice_bits(mmc_card->raw_cid,  24, 32);
        cid->sd_cid.date    = slice_bits(mmc_card->raw_cid,   8, 12);

        printf("manfid(%x), oemid(%x), name(%c%c%c%c%c), rev(%x), serial(%x), date(%x)\n",
               cid->manfid, cid->sd_cid.oemid,
               cid->sd_cid.name[0], cid->sd_cid.name[1], cid->sd_cid.name[2],
               cid->sd_cid.name[3], cid->sd_cid.name[4],
               cid->sd_cid.rev, cid->sd_cid.serial, cid->sd_cid.date);
    } else {
        printf("Not Implemented!\n");
        return -1;
    }

    return 0;
}
#endif

static int mmc_decode_csd(mmc_card_t mmc_card, struct csd *csd)
{
    if (mmc_card == NULL || csd == NULL) {
        return -1;
    }

#define CSD_BITS(start, size) \
    slice_bits(mmc_card->raw_csd, start, size)

    csd->structure = CSD_BITS(126, 2);

    if (csd->structure == 0) {
        printf("CSD Version 1.0\n");
        csd->c_size      = CSD_BITS(62, 12);
        csd->c_size_mult = CSD_BITS(47,  3);
        csd->read_bl_len = CSD_BITS(80,  4);
        csd->tran_speed  = CSD_BITS(96,  8);
    } else if (csd->structure == 1) {
        printf("CSD Version 2.0\n");
        csd->c_size      = CSD_BITS(48, 22);
        csd->c_size_mult = 0;
        csd->read_bl_len = CSD_BITS(80,  4);
        csd->tran_speed  = CSD_BITS(96,  8);
    } else {
        printf("Unknown CSD version!\n");
        return -1;
    }

    return 0;
}

static struct mmc_cmd*
mmc_cmd_new(uint32_t index, uint32_t arg, int rsp_type) {
    struct mmc_cmd* cmd;
    cmd = malloc(sizeof(*cmd));
    if (cmd) {
        /* Command */
        cmd->index = index;
        cmd->arg = arg;
        cmd->rsp_type = rsp_type;
        cmd->data = NULL;
        /* Transaction maintenance */
        cmd->cb = NULL;
        cmd->token = NULL;
        cmd->next = NULL;
        cmd->complete = 0;
    }
    return cmd;
}

static int
mmc_cmd_add_data(struct mmc_cmd* cmd, void* vbuf, uintptr_t pbuf, uint32_t addr,
                 uint32_t block_size, uint32_t blocks)
{
    struct mmc_data* d;
    assert(cmd->data == NULL);
    d = (struct mmc_data*)malloc(sizeof(*d));
    if (d) {
        d->pbuf = pbuf;
        d->vbuf = vbuf;
        d->data_addr = addr;
        d->block_size = block_size;
        d->blocks = blocks;
        cmd->data = d;
        return 0;
    } else {
        return -1;
    }
}

static void
mmc_cmd_destroy(struct mmc_cmd* cmd)
{
    if (cmd->data) {
        free(cmd->data);
    }
    free(cmd);
}

static struct mmc_completion_token*
mmc_new_completion_token(mmc_card_t mmc_card, mmc_cb cb, void* token) {
    struct mmc_completion_token* t;
    t = (struct mmc_completion_token*)malloc(sizeof(*t));
    if (t) {
        t->card = mmc_card;
        t->cb = cb;
        t->token = token;
    }
    return t;
}

static void
mmc_completion_token_destroy(struct mmc_completion_token* t)
{
    free(t);
}

/**
 * MMC/SD/SDIO card registry.
 */
static int
mmc_card_registry(mmc_card_t card)
{
    struct mmc_cmd cmd = {.data = NULL};
    int ret;

    D(DBG_INFO, "\n");

    /* Get card ID */
    cmd.index = MMC_ALL_SEND_CID;
    cmd.arg = 0;
    cmd.rsp_type = MMC_RSP_TYPE_R2;
    ret = host_send_command(card, &cmd, NULL, NULL);
    if (ret) {
        D(DBG_ERR, "No response!\n");
        card->status = CARD_STS_INACTIVE;
        return -1;
    } else {
        card->status = CARD_STS_ACTIVE;
    }

    /* Left shift the response by 8. Consult SDHC manual. */
    cmd.response[3] = ((cmd.response[3] << 8) | (cmd.response[2] >> 24));
    cmd.response[2] = ((cmd.response[2] << 8) | (cmd.response[1] >> 24));
    cmd.response[1] = ((cmd.response[1] << 8) | (cmd.response[0] >> 24));
    cmd.response[0] = (cmd.response[0] << 8);
    memcpy(card->raw_cid, cmd.response, sizeof(card->raw_cid));


    /* Retrieve RCA number. */
    cmd.index = MMC_SEND_RELATIVE_ADDR;
    cmd.arg = 0;
    cmd.rsp_type = MMC_RSP_TYPE_R6;
    host_send_command(card, &cmd, NULL, NULL);
    card->raw_rca = (cmd.response[0] >> 16);
    D(DBG_INFO, "New Card RCA: %x\n", card->raw_rca);

    /* Read CSD, Status */
    cmd.index = MMC_SEND_CSD;
    cmd.arg = card->raw_rca << 16;
    cmd.rsp_type = MMC_RSP_TYPE_R2;
    host_send_command(card, &cmd, NULL, NULL);

    /* Left shift the response by 8. Consult SDHC manual. */
    cmd.response[3] = ((cmd.response[3] << 8) | (cmd.response[2] >> 24));
    cmd.response[2] = ((cmd.response[2] << 8) | (cmd.response[1] >> 24));
    cmd.response[1] = ((cmd.response[1] << 8) | (cmd.response[0] >> 24));
    cmd.response[0] = (cmd.response[0] << 8);
    memcpy(card->raw_csd, cmd.response, sizeof(card->raw_csd));

    cmd.index = MMC_SEND_STATUS;
    cmd.rsp_type = MMC_RSP_TYPE_R1;
    host_send_command(card, &cmd, NULL, NULL);

    /* Select the card */
    cmd.index = MMC_SELECT_CARD;
    cmd.arg = card->raw_rca << 16;
    cmd.rsp_type = MMC_RSP_TYPE_R1b;
    host_send_command(card, &cmd, NULL, NULL);

    /* Set Bus width */
    cmd.index = MMC_APP_CMD;
    cmd.arg = card->raw_rca << 16;
    cmd.rsp_type = MMC_RSP_TYPE_R1;
    host_send_command(card, &cmd, NULL, NULL);
    cmd.index = SD_SET_BUS_WIDTH;
    host_send_command(card, &cmd, NULL, NULL);
    return 0;
}


/**
 * Card voltage validation.
 */
static int
mmc_voltage_validation(mmc_card_t card)
{
    struct mmc_cmd cmd = {.data = NULL};
    int voltage;
    int ret;

    /* Send CMD55 to issue an application specific command. */
    cmd.index = MMC_APP_CMD;
    cmd.arg = 0;
    cmd.rsp_type = MMC_RSP_TYPE_R1;
    ret = host_send_command(card, &cmd, NULL, NULL);
    if (!ret) {
        /* It is a SD card. */
        cmd.index = SD_SD_APP_OP_COND;
        cmd.arg = 0;
        cmd.rsp_type = MMC_RSP_TYPE_R3;
        card->type = CARD_TYPE_SD;
    } else {
        /* It is a MMC card. */
        cmd.index = MMC_SEND_OP_COND;
        cmd.arg = 0;
        cmd.rsp_type = MMC_RSP_TYPE_R3;
        card->type = CARD_TYPE_MMC;
    }
    ret = host_send_command(card, &cmd, NULL, NULL);
    if (ret) {
        card->type = CARD_TYPE_UNKNOWN;
        /* TODO: Be nicer */
        assert(0);
    }
    card->ocr = cmd.response[0];

    /* TODO: Check uSDHC compatibility */
    voltage = MMC_VDD_29_30 | MMC_VDD_30_31;
    if (host_is_voltage_compatible(card, 3300) && (card->ocr & voltage)) {
        /* Voltage compatible */
        voltage |= (1 << 30);
        voltage |= (1 << 25);
        voltage |= (1 << 24);
    }

    /* Wait until the voltage level is set. */
    do {
        if (card->type == CARD_TYPE_SD) {
            cmd.index = MMC_APP_CMD;
            cmd.arg = 0;
            cmd.rsp_type = MMC_RSP_TYPE_R1;
            host_send_command(card, &cmd, NULL, NULL);
        }

        cmd.index = SD_SD_APP_OP_COND;
        cmd.arg = voltage;
        cmd.rsp_type = MMC_RSP_TYPE_R3;
        host_send_command(card, &cmd, NULL, NULL);
        udelay(100000);
    } while (!(cmd.response[0] & (1U << 31)));
    card->ocr = cmd.response[0];

    /* Check CCS bit */
    if (card->ocr & (1 << 30)) {
        card->high_capacity = 1;
    } else {
        card->high_capacity = 0;
    }

    D(DBG_INFO, "Voltage set!\n");
    return 0;
}


static int
mmc_reset(mmc_card_t card)
{
    /* Reset the card with CMD0 */
    struct mmc_cmd cmd = {.data = NULL};
    cmd.index = MMC_GO_IDLE_STATE;
    cmd.arg = 0;
    cmd.rsp_type = MMC_RSP_TYPE_NONE;
    host_send_command(card, &cmd, NULL, NULL);

    /* TODO: review this command. */
    cmd.index = MMC_SEND_EXT_CSD;
    cmd.arg = 0x1AA;
    cmd.rsp_type = MMC_RSP_TYPE_R1;
    host_send_command(card, &cmd, NULL, NULL);
    return 0;
}

static void
mmc_blockop_completion_cb(struct sdio_host_dev* sdio, int stat, struct mmc_cmd* cmd,
                          void* token)
{
    struct mmc_completion_token *t;
    size_t bytes;

    t = (struct mmc_completion_token*)token;
    if (stat == 0) {
        bytes = cmd->data->block_size * cmd->data->blocks;
    } else {
        bytes = 0;
    }
    /* Call the registered function */
    t->cb(t->card, stat, bytes, t->token);
    /* Free memory */
    mmc_cmd_destroy(cmd);
    mmc_completion_token_destroy(t);
}

int
mmc_init(sdio_host_dev_t* sdio, ps_io_ops_t *io_ops, mmc_card_t* mmc_card)
{
    mmc_card_t mmc;

    /* Allocate the mmc card structure */
    mmc = (mmc_card_t)malloc(sizeof(*mmc));
    assert(mmc);
    if (!mmc) {
        return -1;
    }
    mmc->dalloc = &io_ops->dma_manager;
    mmc->sdio = sdio;
    /* Reset the host controller */
    if (host_reset(mmc)) {
        LOG_ERROR("Failed to reset host controller\n");
        free(mmc);
        return -1;
    }
    /* Initialise the card */
    if (mmc_reset(mmc)) {
        LOG_ERROR("Failed to reset SD/MMC card\n");
        free(mmc);
        return -1;
    }
    if (mmc_voltage_validation(mmc)) {
        LOG_ERROR("Failed to perform voltage validation\n");
        free(mmc);
        return -1;
    }
    /* Register the card */
    if (mmc_card_registry(mmc)) {
        LOG_ERROR("Failed to register card\n");
        free(mmc);
        return -1;
    }

    *mmc_card = mmc;
    assert(mmc);
    return 0;
}

long
mmc_block_read(mmc_card_t mmc_card, unsigned long start,
               int nblocks, void* vbuf, uintptr_t pbuf, mmc_cb cb, void* token)
{
    struct mmc_cmd* cmd;
    uint32_t arg;
    int bs;
    bs = mmc_block_size(mmc_card);
    /* Determine command argument */
    if (mmc_card->high_capacity) {
        arg = start;
    } else {
        arg = start + bs;
    }
    /* Allocate command structure */
    cmd = mmc_cmd_new(MMC_READ_SINGLE_BLOCK, arg, MMC_RSP_TYPE_R1);
    if (cmd == NULL) {
        return -1;
    }
    /* Add a data segment */
    if (mmc_cmd_add_data(cmd, vbuf, pbuf, start, bs, nblocks)) {
        mmc_cmd_destroy(cmd);
        return -1;
    }
    /* Send the command */
    if (cb) {
        struct mmc_completion_token* mmc_token;
        unsigned long ret;
        mmc_token = mmc_new_completion_token(mmc_card, cb, token);
        if (mmc_token == NULL) {
            mmc_cmd_destroy(cmd);
            return -1;
        }
        ret = host_send_command(mmc_card, cmd, &mmc_blockop_completion_cb,
                                mmc_token);
        if (ret) {
            mmc_completion_token_destroy(mmc_token);
            mmc_cmd_destroy(cmd);
        }
        return ret;
    } else {
        unsigned long ret;
        ret = host_send_command(mmc_card, cmd, NULL, NULL);
        return (ret) ? ret : bs * nblocks;
    }
}


long
mmc_block_write(mmc_card_t mmc_card, unsigned long start, int nblocks,
                const void* vbuf, uintptr_t pbuf, mmc_cb cb, void* token)
{
    struct mmc_cmd* cmd;
    uint32_t arg;
    int bs;
    bs = mmc_block_size(mmc_card);
    /* Determine command argument */
    if (mmc_card->high_capacity) {
        arg = start;
    } else {
        arg = start + bs;
    }
    /* Allocate command structure */
    cmd = mmc_cmd_new(MMC_WRITE_BLOCK, arg, MMC_RSP_TYPE_R1);
    if (cmd == NULL) {
        return -1;
    }

    /* Add a data segment */
    if (mmc_cmd_add_data(cmd, (void*)vbuf, pbuf, start, bs, nblocks)) {
        mmc_cmd_destroy(cmd);
        return -1;
    }

    /* Send the command */
    if (cb) {
        struct mmc_completion_token* mmc_token;
        unsigned long ret;
        mmc_token = mmc_new_completion_token(mmc_card, cb, token);
        if (mmc_token == NULL) {
            mmc_cmd_destroy(cmd);
            return -1;
        }
        ret = host_send_command(mmc_card, cmd, &mmc_blockop_completion_cb,
                                mmc_token);
        if (ret) {
            mmc_completion_token_destroy(mmc_token);
            mmc_cmd_destroy(cmd);
        }
        return ret;
    } else {
        unsigned long ret;
        ret = host_send_command(mmc_card, cmd, NULL, NULL);
        mmc_cmd_destroy(cmd);
        return (ret) ? ret : bs * nblocks;
    }
}

unsigned long long
mmc_card_capacity(mmc_card_t mmc_card)
{
    int ret;
    unsigned long long capacity;
    struct csd csd;

    ret = mmc_decode_csd(mmc_card, &csd);
    if (ret) {
        return -1;
    }

    if (csd.structure == 0) {
        capacity = (csd.c_size + 1) * (1U << (csd.c_size_mult + 2));
        capacity *= (1U << csd.read_bl_len);
    } else if (csd.structure == 1) {
        capacity = (csd.c_size + 1) * 512 * 1024;
    } else {
        return -1;
    }

    return capacity;
}


int
mmc_nth_irq(mmc_card_t mmc, int n)
{
    return host_nth_irq(mmc, n);
}

int
mmc_handle_irq(mmc_card_t mmc, int irq)
{
    return host_handle_irq(mmc, irq);
}



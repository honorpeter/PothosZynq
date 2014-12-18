// Copyright (c) 2014-2014 Josh Blum
// SPDX-License-Identifier: BSL-1.0

#include "pothos_zynq_dma_module.h"
#include <linux/uaccess.h> //copy_to/from_user
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

static pothos_zynq_dma_buff_t pothos_zynq_dma_buff_alloc(struct platform_device *pdev, const size_t size)
{
    pothos_zynq_dma_buff_t buff;
    dma_addr_t phys_addr = 0;
    void *virt_addr = dma_zalloc_coherent(&pdev->dev, size, &phys_addr, GFP_KERNEL);
    buff.paddr = phys_addr;
    buff.kaddr = virt_addr;
    buff.uaddr = NULL; //filled by user with mmap
    return buff;
}

long pothos_zynq_dma_ioctl_alloc(pothos_zynq_dma_user_t *user, const pothos_zynq_dma_alloc_t *user_config)
{
    pothos_zynq_dma_chan_t *chan = user->chan;
    struct platform_device *pdev = user->engine->pdev;

    //copy the buffer into kernel space
    pothos_zynq_dma_alloc_t alloc_args;
    if (copy_from_user(&alloc_args, user_config, sizeof(pothos_zynq_dma_alloc_t)) != 0) return -EACCES;

    //check the sentinel
    if (alloc_args.sentinel != POTHOS_ZYNQ_DMA_SENTINEL) return -EINVAL;

    //are we already allocated?
    if (chan->allocs.buffs != NULL) return -EBUSY;

    //copy the dma buffers array into kernel space
    chan->allocs.num_buffs = alloc_args.num_buffs;
    chan->allocs.buffs = devm_kzalloc(&pdev->dev, alloc_args.num_buffs*sizeof(pothos_zynq_dma_buff_t), GFP_KERNEL);
    if (copy_from_user(chan->allocs.buffs, alloc_args.buffs, alloc_args.num_buffs*sizeof(pothos_zynq_dma_buff_t)) != 0) return -EACCES;

    //allocate dma buffers
    for (size_t i = 0; i < chan->allocs.num_buffs; i++)
    {
        chan->allocs.buffs[i] = pothos_zynq_dma_buff_alloc(pdev, chan->allocs.buffs[i].bytes);
    }

    //allocate SG table
    chan->sgbuff = pothos_zynq_dma_buff_alloc(pdev, sizeof(xilinx_dma_desc_t)*chan->allocs.num_buffs);
    chan->sgtable = (xilinx_dma_desc_t *)chan->sgbuff.kaddr;

    return 0;
}

long pothos_zynq_dma_ioctl_free(pothos_zynq_dma_user_t *user, const pothos_zynq_dma_free_t *user_config)
{
    pothos_zynq_dma_chan_t *chan = user->chan;
    struct platform_device *pdev = user->engine->pdev;

    //copy the buffer into kernel space
    pothos_zynq_dma_free_t free_args;
    if (copy_from_user(&free_args, user_config, sizeof(pothos_zynq_dma_free_t)) != 0) return -EACCES;

    //check the sentinel
    if (free_args.sentinel != POTHOS_ZYNQ_DMA_SENTINEL) return -EINVAL;

    //are we already free?
    if (chan->allocs.buffs == NULL) return 0;

    //free dma buffers
    for (size_t i = 0; i < chan->allocs.num_buffs; i++)
    {
        if (chan->allocs.buffs[i].kaddr == NULL) continue; //alloc failed eariler
        dma_free_coherent(&pdev->dev, chan->allocs.buffs[i].bytes, chan->allocs.buffs[i].kaddr, chan->allocs.buffs[i].paddr);
    }

    //free the SG buffer
    dma_free_coherent(&pdev->dev, chan->sgbuff.bytes, chan->sgbuff.kaddr, chan->sgbuff.paddr);
    chan->sgtable = NULL;

    //free the dma buffer structures
    devm_kfree(&pdev->dev, chan->allocs.buffs);
    chan->allocs.num_buffs = 0;
    chan->allocs.buffs = NULL;

    return 0;
}

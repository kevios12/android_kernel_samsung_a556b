// SPDX-License-Identifier: GPL-2.0
/*
 * debug-snapshot-debug-kinfo.c - prepare memory for the debug-kinfo driver
 *
 * Copyright 2020 Google LLC
 * Copyright (c) 2021 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 */

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>

#include <asm/pgtable.h>
#include <generated/utsrelease.h>
#include <soc/samsung/exynos/debug-snapshot.h>
#include "../../../../drivers/android/debug_kinfo.h"

struct vendor_kernel_info {
	/* For linux banner */
	__u8 uts_release[__NEW_UTS_LEN];
} __packed;

struct vendor_kernel_all_info {
	__u32 magic_number;
	__u32 combined_checksum;
	struct vendor_kernel_info info;
} __packed;

static void update_vendor_kernel_all_info(struct vendor_kernel_all_info *all_info)
{
	int index;
	struct vendor_kernel_info *info;
	u32 *vendor_checksum_info;

	all_info->magic_number = DEBUG_KINFO_MAGIC;
	all_info->combined_checksum = 0;

	info = &all_info->info;
	vendor_checksum_info = (u32 *)info;
	for (index = 0; index < sizeof(*info) / sizeof(u32); index++)
		all_info->combined_checksum ^= vendor_checksum_info[index];
}

static int debug_snapshot_debug_kinfo_probe(struct platform_device *pdev)
{
	int i;
	struct reserved_mem *rmem;
	struct device_node *mem_region;
	phys_addr_t base, size, offset;
	unsigned int num_pages;
	unsigned long flags = VM_MAP;
	struct page **pages;
	void *vaddr;
	struct vendor_kernel_all_info *all_info;
	struct vendor_kernel_info *info;

	mem_region = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!mem_region) {
		dev_warn(&pdev->dev, "no such memory-region\n");
		return 0;
	}

	rmem = of_reserved_mem_lookup(mem_region);
	if (!rmem) {
		dev_warn(&pdev->dev, "no such reserved mem of node name %s\n",
				pdev->dev.of_node->name);
		return 0;
	}

	base = rmem->base;
	size = rmem->size;
	offset = size / 2;

	if (!base || !size || (size - offset) < sizeof(struct vendor_kernel_all_info)) {
		dev_warn(&pdev->dev,
			"unexpected reserved memory(base[%pa], size [%pa])\n",
			&base, &size);
		return 0;
	}

	num_pages = (size >> PAGE_SHIFT);
	if (size > (num_pages << PAGE_SHIFT))
		num_pages++;

	pages = kcalloc(num_pages, sizeof(struct page *), GFP_KERNEL);
	for (i = 0; i < num_pages; i++) {
		pages[i] = phys_to_page(base);
		base += PAGE_SIZE;
	}

	vaddr = vmap(pages, num_pages, flags, pgprot_writecombine(PAGE_KERNEL));
	kfree(pages);
	if (!vaddr) {
		dev_warn(&pdev->dev, "paddr:%pa page_size:%pa failed to vmap\n",
				&rmem->base, &rmem->size);
		return 0;
	}

	/* Prepare for AOSP kernel info. backup */
	rmem->priv = vaddr;

	/* Backup vendor kernel info. */
	all_info = (struct vendor_kernel_all_info *)(vaddr + offset);
	memset(all_info, 0, sizeof(*all_info));
	info = &all_info->info;
	strscpy(info->uts_release, UTS_RELEASE, sizeof(info->uts_release));
	update_vendor_kernel_all_info(all_info);

	return 0;
}

static const struct of_device_id debug_snapshot_debug_kinfo_of_match[] = {
	{ .compatible	= "samsung,debug-snapshot-debug-kinfo" },
	{},
};
MODULE_DEVICE_TABLE(of, debug_snapshot_debug_kinfo_of_match);

static struct platform_driver debug_snapshot_debug_kinfo_driver = {
	.probe = debug_snapshot_debug_kinfo_probe,
	.driver = {
		.name = "debug-snapshot-debug-kinfo",
		.of_match_table = of_match_ptr(debug_snapshot_debug_kinfo_of_match),
	},
};
module_platform_driver(debug_snapshot_debug_kinfo_driver);

MODULE_DESCRIPTION("Debug Snapshot Debug Kinfo");
MODULE_LICENSE("GPL v2");

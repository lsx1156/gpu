/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_CLEAR_BLIT_H
#define TU_CLEAR_BLIT_H

#include "tu_common.h"

void tu_init_clear_blit_shaders(struct tu_device *dev);

void tu_destroy_clear_blit_shaders(struct tu_device *dev);

template <chip CHIP>
void
tu6_clear_lrz(struct tu_cmd_buffer *cmd, struct tu_cs *cs, struct tu_image* image, const VkClearValue *value);

template <chip CHIP>
void
tu6_dirty_lrz_fc(struct tu_cmd_buffer *cmd, struct tu_cs *cs, struct tu_image* image);

template <chip CHIP>
void
tu_resolve_sysmem(struct tu_cmd_buffer *cmd,
                  struct tu_cs *cs,
                  const struct tu_image_view *src,
                  const struct tu_image_view *dst,
                  uint32_t layer_mask,
                  uint32_t layers,
                  const VkRect2D *rect);

template <chip CHIP>
void
tu_clear_sysmem_attachment(struct tu_cmd_buffer *cmd,
                           struct tu_cs *cs,
                           uint32_t a);

template <chip CHIP>
void
tu_clear_gmem_attachment(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         uint32_t a);

template <chip CHIP>
void
tu_load_gmem_attachment(struct tu_cmd_buffer *cmd,
                        struct tu_cs *cs,
                        uint32_t a,
                        bool cond_exec_allowed,
                        bool force_load);

/* note: gmem store can also resolve */
template <chip CHIP>
void
tu_store_gmem_attachment(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         uint32_t a,
                         uint32_t gmem_a,
                         uint32_t layers,
                         uint32_t layer_mask,
                         bool cond_exec_allowed);

void
tu_choose_gmem_layout(struct tu_cmd_buffer *cmd);

/* 芯片 5（a5xx）没有 RB_2D 寄存器：buffer->image 的拷贝退回 CPU 逐行复制，
 * 在队列提交前执行（sysmem + 全内存 host-visible）。
 */
struct tu5xx_cpu_copy;

void
tu5xx_cpu_copy_add_to_image(struct tu_cmd_buffer *cmd,
                            struct tu_buffer *buf,
                            struct tu_image *img,
                            uint32_t region_count,
                            const VkBufferImageCopy2 *regions);

void
tu5xx_cpu_copy_add_to_buffer(struct tu_cmd_buffer *cmd,
                             struct tu_image *img,
                             struct tu_buffer *buf,
                             uint32_t region_count,
                             const VkBufferImageCopy2 *regions);

/* 提交前：只跑 buffer->image */
void
tu5xx_cpu_copy_run_pre(struct tu_cmd_buffer *cmd);

/* 提交后把 image->buffer 节点搬到设备级待办链（不在这里等 GPU） */
void
tu5xx_cpu_copy_detach_readbacks(struct tu_cmd_buffer *cmd, struct tu_device *dev);

/* 等队列空闲并执行挂起的读回（vkMapMemory 时调用） */
void
tu5xx_cpu_readback_drain(struct tu_device *dev);

/* 设备销毁时丢弃未消费的读回节点 */
void
tu5xx_cpu_readback_free_all(struct tu_device *dev);

/* 记录“这条命令写入了该图像”，供读回安全检查用 */
void
tu5xx_note_image_write(struct tu_cmd_buffer *cmd, struct tu_image *img);

void
tu5xx_cpu_copy_free(struct tu_cmd_buffer *cmd);

#endif /* TU_CLEAR_BLIT_H */

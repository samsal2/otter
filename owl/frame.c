#include <owl/code.h>
#include <owl/frame.h>
#include <owl/internal.h>
#include <owl/renderer_internal.h>
#include <owl/types.h>

#define OWL_VK_TIMEOUT (OwlU64) - 1

enum owl_code owl_begin_frame(struct owl_renderer *renderer) {
  enum owl_code err = OWL_SUCCESS;
  OwlU32 const active = renderer->dyn_active_buf;

  {
    VkResult const result = vkAcquireNextImageKHR(
        renderer->device, renderer->swapchain, OWL_VK_TIMEOUT,
        renderer->frame_img_available[active], VK_NULL_HANDLE,
        &renderer->swapchain_active_img);

    if (VK_ERROR_OUT_OF_DATE_KHR == result) {
      err = OWL_ERROR_OUTDATED_SWAPCHAIN;
      goto end;
    }

    if (VK_SUBOPTIMAL_KHR == result) {
      err = OWL_ERROR_OUTDATED_SWAPCHAIN;
      goto end;
    }

    if (VK_ERROR_SURFACE_LOST_KHR == result) {
      err = OWL_ERROR_OUTDATED_SWAPCHAIN;
      goto end;
    }
  }

  {
    OWL_VK_CHECK(vkWaitForFences(renderer->device, 1,
                                 &renderer->frame_in_flight[active], VK_TRUE,
                                 OWL_VK_TIMEOUT));

    OWL_VK_CHECK(vkResetFences(renderer->device, 1,
                               &renderer->frame_in_flight[active]));

    OWL_VK_CHECK(vkResetCommandPool(renderer->device,
                                    renderer->frame_cmd_pools[active], 0));
  }

  {
    VkCommandBufferBeginInfo begin;

    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.pNext = NULL;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin.pInheritanceInfo = NULL;

    OWL_VK_CHECK(
        vkBeginCommandBuffer(renderer->frame_cmd_bufs[active], &begin));
  }

  {
    VkRenderPassBeginInfo begin;
    OwlU32 const img = renderer->swapchain_active_img;

    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.pNext = NULL;
    begin.renderPass = renderer->main_render_pass;
    begin.framebuffer = renderer->framebuffers[img];
    begin.renderArea.offset.x = 0;
    begin.renderArea.offset.y = 0;
    begin.renderArea.extent = renderer->swapchain_extent;
    begin.clearValueCount = OWL_ARRAY_SIZE(renderer->swapchain_clear_vals);
    begin.pClearValues = renderer->swapchain_clear_vals;

    vkCmdBeginRenderPass(renderer->frame_cmd_bufs[active], &begin,
                         VK_SUBPASS_CONTENTS_INLINE);
  }

end:
  return err;
}
#undef OWL_VK_TIMEOUT

enum owl_code owl_end_frame(struct owl_renderer *renderer) {
  enum owl_code err = OWL_SUCCESS;
  OwlU32 const active = renderer->dyn_active_buf;

  {
    vkCmdEndRenderPass(renderer->frame_cmd_bufs[active]);
    OWL_VK_CHECK(vkEndCommandBuffer(renderer->frame_cmd_bufs[active]));
  }

  {
#define OWL_VK_WAIT_STAGE VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    VkSubmitInfo submit;
    VkPipelineStageFlags const stage = OWL_VK_WAIT_STAGE;

    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.pNext = NULL;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &renderer->frame_img_available[active];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderer->frame_render_done[active];
    submit.pWaitDstStageMask = &stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &renderer->frame_cmd_bufs[active];

    OWL_VK_CHECK(vkQueueSubmit(renderer->graphics_queue, 1, &submit,
                               renderer->frame_in_flight[active]));
#undef OWL_VK_WAIT_STAGE
  }

  {
    VkResult result;
    VkPresentInfoKHR present;

    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext = NULL;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderer->frame_render_done[active];
    present.swapchainCount = 1;
    present.pSwapchains = &renderer->swapchain;
    present.pImageIndices = &renderer->swapchain_active_img;
    present.pResults = NULL;

    result = vkQueuePresentKHR(renderer->present_queue, &present);

    if (VK_ERROR_OUT_OF_DATE_KHR == result) {
      err = OWL_ERROR_OUTDATED_SWAPCHAIN;
      goto end;
    }

    if (VK_SUBOPTIMAL_KHR == result) {
      err = OWL_ERROR_OUTDATED_SWAPCHAIN;
      goto end;
    }

    if (VK_ERROR_SURFACE_LOST_KHR == result) {
      err = OWL_ERROR_OUTDATED_SWAPCHAIN;
      goto end;
    }
  }

  /* swap actives */
  if (OWL_DYN_BUF_COUNT == ++renderer->dyn_active_buf)
    renderer->dyn_active_buf = 0;

  /* reset offset */
  renderer->dyn_offsets[renderer->dyn_active_buf] = 0;

  owl_clear_garbage(renderer);

end:
  return err;
}

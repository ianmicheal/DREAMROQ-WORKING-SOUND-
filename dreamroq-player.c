/*
 * Dreamroq by Mike Melanson
 * Updated by Josh Pearson to add audio support
 *
 * This is the sample Dreamcast player app, designed to be run under
 * the KallistiOS operating system.
 */
/*
	Name: Ian micheal
	Copyright: 
	Author: Ian micheal
	Date: 12/08/23 05:17
	Description: kos 2.0 up port threading fix and wrappers and all warnings fixed
	Redone threading and main added benchmarking for timing acia and roq decoding audio
	redone rendering order and code commented to be much easier to read.
	example OUTPUT:> Wait for AICA Driver: 88 ms
    OUTPUT:> Wait for RoQ Decoder: 1 ms
    OUTPUT:> Copy PCM Samples: 1 ms
    OUTPUT:> Inform AICA Driver: 0 ms
    OUTPUT:> Wait for AICA Driver: 88 ms
    OUTPUT:> Wait for RoQ Decoder: 0 ms
    
    Before 
    OUTPUT:> Wait for AICA Driver: 168 ms
    OUTPUT:> Wait for RoQ Decoder: 0 ms
    OUTPUT:> Copy PCM Samples: 1 ms
    OUTPUT:> Inform AICA Driver: 0 ms
    OUTPUT:> Wait for AICA Driver: 187 ms
    OUTPUT:> Wait for RoQ Decoder: 0 ms
    OUTPUT:> Copy PCM Samples: 1 ms
    OUTPUT:> Inform AICA Driver: 0 ms
    OUTPUT:> Wait for AICA Driver: 197 ms
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kos.h>
#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include "dreamroqlib.h"
#include "dc_timer.h"
#include "libdcmc/snddrv.h"
#include <dc/sound/sound.h>
#include <stdio.h>


static unsigned char *pcm_buf = NULL;
static int pcm_size = 0;
#define AUDIO_THREAD_PRIO 0
kthread_t *audio_thread; // Thread handle for the audio thread
int audio_init = 0; // Flag to indicate audio initialization status
static mutex_t pcm_mut = MUTEX_INITIALIZER;
/* Video Global variables */
static pvr_ptr_t textures[2];
static int current_frame = 0;
static int graphics_initialized = 0;
static float video_delay;
// Define the target frame rate
#define TARGET_FRAME_RATE 31

// Define a preprocessor macro for enabling or disabling debugging
#define DEBUG_SND_THD 0  // Set to 1 to enable debugging, 0 to disable

static void snd_thd()
{
    do
    {
        unsigned int start_time, end_time;

        // Measure time taken by waiting for AICA Driver request
        start_time = dc_get_time();
        
        while (snddrv.buf_status != SNDDRV_STATUS_NEEDBUF)
            thd_pass();
        end_time = dc_get_time();
        
        #if DEBUG_SND_THD
        printf("Wait for AICA Driver: %u ms\n", end_time - start_time);
        #endif

        // Measure time taken by waiting for RoQ Decoder
        start_time = dc_get_time();
        while (pcm_size < snddrv.pcm_needed)
        {
            if (snddrv.dec_status == SNDDEC_STATUS_DONE)
                goto done;
            thd_pass();
        }
        end_time = dc_get_time();
        
        #if DEBUG_SND_THD
        printf("Wait for RoQ Decoder: %u ms\n", end_time - start_time);
        #endif

        // Measure time taken by copying PCM samples
        start_time = dc_get_time();
        mutex_lock(&pcm_mut);
        memcpy(snddrv.pcm_buffer, pcm_buf, snddrv.pcm_needed);
        pcm_size -= snddrv.pcm_needed;
        memmove(pcm_buf, pcm_buf + snddrv.pcm_needed, pcm_size);
        mutex_unlock(&pcm_mut);
        end_time = dc_get_time();
        
        #if DEBUG_SND_THD
        printf("Copy PCM Samples: %u ms\n", end_time - start_time);
        #endif

        // Measure time taken by informing AICA Driver
        start_time = dc_get_time();
        snddrv.buf_status = SNDDRV_STATUS_HAVEBUF;
        end_time = dc_get_time();
        
        #if DEBUG_SND_THD
        printf("Inform AICA Driver: %u ms\n", end_time - start_time);
        #endif

    } while (snddrv.dec_status == SNDDEC_STATUS_STREAMING);
done:
    snddrv.dec_status = SNDDEC_STATUS_NULL;
}


static int render_cb(unsigned short *buf, int width, int height, int stride,
                     int texture_height)
{
    pvr_poly_cxt_t cxt;
    static pvr_poly_hdr_t hdr[2];
    static pvr_vertex_t vert[4];

    float ratio;
    // screen coordinates of upper left and bottom right corners
    static int ul_x, ul_y, br_x, br_y;

    // Initialize textures, drawing coordinates, and other parameters
    if (!graphics_initialized)
    {
        textures[0] = pvr_mem_malloc(stride * texture_height * 2);
        textures[1] = pvr_mem_malloc(stride * texture_height * 2);
        if (!textures[0] || !textures[1])
        {
            return ROQ_RENDER_PROBLEM;
        }

        // Precompile the poly headers
        for (int i = 0; i < 2; i++) {
            pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                             stride, texture_height, textures[i], PVR_FILTER_NONE);
            pvr_poly_compile(&hdr[i], &cxt);
        }

        // Calculate drawing coordinates
        ratio = 640.0 / width;
        ul_x = 0;
        br_x = (int)(ratio * stride);
        ul_y = (int)((480 - ratio * height) / 2);
        br_y = ul_y + (int)(ratio * texture_height);

        // Set common vertex properties
        for (int i = 0; i < 4; i++) {
            vert[i].z = 1.0f;
            vert[i].argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
            vert[i].oargb = 0;
            vert[i].flags = (i < 3) ? PVR_CMD_VERTEX : PVR_CMD_VERTEX_EOL;
        }

        // Initialize vertex coordinates and UV coordinates
        vert[0].x = ul_x;
        vert[0].y = ul_y;
        vert[0].u = 0.0;
        vert[0].v = 0.0;

        vert[1].x = br_x;
        vert[1].y = ul_y;
        vert[1].u = 1.0;
        vert[1].v = 0.0;

        vert[2].x = ul_x;
        vert[2].y = br_y;
        vert[2].u = 0.0;
        vert[2].v = 1.0;

        vert[3].x = br_x;
        vert[3].y = br_y;
        vert[3].u = 1.0;
        vert[3].v = 1.0;

        // Get the current hardware timing
        video_delay = (float)dc_get_time();

        graphics_initialized = 1;
    }
   dcache_flush_range((uintptr_t)textures[current_frame], (uintptr_t)(textures[current_frame] + stride * texture_height * 2));

    // Send the video frame as a texture over to video RAM
   pvr_txr_load_dma(buf, textures[current_frame], stride * texture_height * 2, 1, NULL, 0);

    // Calculate the elapsed time since the last frame
    unsigned int current_time = dc_get_time();
    unsigned int elapsed_time = current_time - video_delay;
    unsigned int target_frame_time = 1000 / TARGET_FRAME_RATE;

    // If the elapsed time is less than the target frame time, introduce a delay
    if (elapsed_time < target_frame_time) {
        unsigned int delay_time = target_frame_time - elapsed_time;
        thd_sleep(delay_time);
    }



    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);

    // Render the frame using precompiled headers and vertices
    pvr_prim(&hdr[current_frame], sizeof(pvr_poly_hdr_t));
    for (int i = 0; i < 4; i++) {
        pvr_prim(&vert[i], sizeof(pvr_vertex_t));
    }

    pvr_list_finish();
    pvr_scene_finish();
    // Update the hardware timing for the current frame
    video_delay = (float)current_time;
    // Toggle between frames
    current_frame = 1 - current_frame;

    return ROQ_SUCCESS;
}



static int audio_cb(unsigned char *buf, int size, int channels)
{
    // Copy the decoded PCM samples to our local PCM buffer
    mutex_lock(&pcm_mut);
    memcpy(pcm_buf + pcm_size, buf, size);
    pcm_size += size;
    mutex_unlock(&pcm_mut);

    return ROQ_SUCCESS;
}

// Audio thread function
static void *snd_thd_wrapper(void *arg)
{
    printf("Audio Thread: Started\n");
    unsigned int start_time = dc_get_time();

    // Call the actual audio thread function
    snd_thd();

    unsigned int end_time = dc_get_time();
    unsigned int elapsed_time = end_time - start_time;
  //  printf("Audio Thread: Finished (Time: %u ms)\n", elapsed_time);

    return NULL;
}


static int quit_cb()
{
    // Check if the video has ended and the audio decoding status is done
    if (snddrv.dec_status == SNDDEC_STATUS_DONE) {
        printf("Exiting due to audio decoding status\n");
        return 1; // Exit the loop
    }

    // Check if the "Start" button is pressed
    MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
    if (st->buttons & CONT_START) {
        printf("Exiting due to Start button\n");
        return 1; // Exit the loop
    }
    MAPLE_FOREACH_END()

  // printf("Continuing loop\n");
    fflush(stdout); // Flush the output buffer to ensure immediate display
   

    return 0; // Continue the loop
}



int main()
{
    int status = 0;

    vid_set_mode(DM_640x480, PM_RGB565);
    pvr_init_defaults();

    printf("dreamroq_play(C) Multimedia Mike Melanson & Josh PH3NOM Pearson 2011\n");
    printf("dreamroq_play(C) Ian micheal Up port to Kos2.0 sound fix and threading\n");
    printf("dreamroq_play(C) Ian micheal Kos2.0 free and exit when loop ends 2023\n");
    printf("dreamroq_play(C) Ian micheal redo frame limit code and rendering and comment what it does 2023\n");
    
    // Initialize audio resources and create the audio thread
    if (!audio_init)
    {
        pcm_buf = malloc(PCM_BUF_SIZE);
        if (pcm_buf == NULL)
        {
            printf("Failed to allocate PCM buffer\n");
            return 1;
        }

        snddrv_start(22050, 2);
        snddrv.dec_status = SNDDEC_STATUS_STREAMING;

        printf("Creating Audio Thread\n");
        audio_thread = thd_create(AUDIO_THREAD_PRIO, snd_thd_wrapper, NULL); 
        if (!audio_thread)
        {
            printf("Failed to create audio thread\n");
            free(pcm_buf);
            pcm_buf = NULL;
            return 1;
        }

        audio_init = 1;
    }

    /* To disable a callback, simply replace the function name by 0 */
    status = dreamroq_play("/cd/movie.roq", 0, render_cb, audio_cb, quit_cb);

    printf("dreamroq_play() status = %d\n", status);

    // Terminate and clean up the audio thread
    if (audio_init)
    {
        snddrv.dec_status = SNDDEC_STATUS_DONE;
        while (snddrv.dec_status != SNDDEC_STATUS_NULL)
        {
            thd_sleep(100);
            printf("Waiting for audio thread to finish...\n");
        }
        thd_destroy(audio_thread); // Destroy the audio thread
        free(pcm_buf);
        pcm_buf = NULL;
        pcm_size = 0;
    }

    if (graphics_initialized)
    {
        pvr_mem_free(textures[0]);
        pvr_mem_free(textures[1]);
        printf("Freed PVR memory\n");
    }

    printf("Exiting main()\n");
    pvr_shutdown(); // Clean up PVR resources
    vid_shutdown(); // This function reinitializes the video system to what dcload and friends expect it to be.
    arch_exit();
    return 0;
}





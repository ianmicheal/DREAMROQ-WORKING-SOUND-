/*
 * Dreamroq by Mike Melanson
 * Updated by Josh Pearson to add audio support
 * 
 * This is the main playback engine.
 */
/*
	Name:Ian micheal 
	Date: 15/08/23 08:16
	Description: kos filesystem api port 
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dc/fmath_base.h>
#include <kos/fs.h> // Include the KOS filesystem header
#include "dreamroqlib.h"

#define RoQ_INFO           0x1001
#define RoQ_QUAD_CODEBOOK  0x1002
#define RoQ_QUAD_VQ        0x1011
#define RoQ_SOUND_MONO     0x1020
#define RoQ_SOUND_STEREO   0x1021
#define RoQ_SIGNATURE      0x1084

#define CHUNK_HEADER_SIZE 8

#define LE_16(buf) (*buf | (*(buf+1) << 8))
#define LE_32(buf) (*buf | (*(buf+1) << 8) | (*(buf+2) << 16) | (*(buf+3) << 24))

#define MAX_BUF_SIZE (64 * 1024)


#define ROQ_CODEBOOK_SIZE 256

struct roq_audio
{
     int pcm_samples;
     int channels;
     int position;
     short snd_sqr_arr[260];
     unsigned char pcm_sample[MAX_BUF_SIZE];
}roq_audio;

typedef struct
{
    int width;
    int height;
    int mb_width;
    int mb_height;
    int mb_count;

    int current_frame;
    unsigned short *frame[2];
    int stride;
    int texture_height;

    unsigned short cb2x2[ROQ_CODEBOOK_SIZE][4];
    unsigned short cb4x4[ROQ_CODEBOOK_SIZE][16];

    // Codebook LUT
    int cr_r_lut[256];
    int cb_b_lut[256];
    int cr_g_lut[256];
    int cb_g_lut[256];
    int yy_lut[256];

    // Video Decoding LUT
    int unpack_4x4_lut[4];
    int block_offset_lut[4];
    int subblock_offset_lut[4];
    int upsample_offset_lut[16];
} roq_state;



static int roq_unpack_quad_codebook(unsigned char *buf, int size, int arg, 
    roq_state *state)
{
    int y[4];
    int yp, u, v;
    int r, g, b;
    int count2x2;
    int count4x4;
    int i, j;
    unsigned short *v2x2;
    unsigned short *v4x4;

    count2x2 = (arg >> 8) & 0xFF;
    count4x4 =  arg       & 0xFF;

    if (!count2x2)
        count2x2 = ROQ_CODEBOOK_SIZE;
    /* 0x00 means 256 4x4 vectors iff there is enough space in the chunk
     * after accounting for the 2x2 vectors */
    if (!count4x4 && count2x2 * 6 < size)
        count4x4 = ROQ_CODEBOOK_SIZE;

    /* size sanity check */
    if ((count2x2 * 6 + count4x4 * 4) != size)
    {
        return ROQ_BAD_CODEBOOK;
    }

    /* unpack the 2x2 vectors */
    for (i = 0; i < count2x2; i++)
    {
        /* unpack the YUV components from the bytestream */
        for (j = 0; j < 4; j++)
            y[j] = *buf++;
        u  = *buf++;
        v  = *buf++;

        /* convert to RGB565 */
        for (j = 0; j < 4; j++) {
            yp = state->yy_lut[y[j]];
            r = yp + state->cr_r_lut[v];
            g = yp + state->cr_g_lut[v] + state->cb_g_lut[u];  
            b = yp + state->cb_b_lut[u]; 

            r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
            g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
            b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

            state->cb2x2[i][j] = ((unsigned short)r & 0xf8) << 8 | 
                                 ((unsigned short)g & 0xfc) << 3 | 
                                 ((unsigned short)b & 0xf8) >> 3;
        }
    }

    /* unpack the 4x4 vectors */
    for (i = 0; i < count4x4; i++) {
        for (j = 0; j < 4; j++) {
            v2x2 = state->cb2x2[*buf++];
            v4x4 = state->cb4x4[i] + state->unpack_4x4_lut[j];
            v4x4[0] = v2x2[0];
            v4x4[1] = v2x2[1];
            v4x4[4] = v2x2[2];
            v4x4[5] = v2x2[3];
        }
    }

    return ROQ_SUCCESS;
}

#define GET_BYTE(x) \
    if (index >= size) { \
        status = ROQ_BAD_VQ_STREAM; \
        x = 0; \
    } else { \
        x = buf[index++]; \
    }

#define GET_MODE() \
    if (!mode_count) { \
        GET_BYTE(mode_lo); \
        GET_BYTE(mode_hi); \
        mode_set = (mode_hi << 8) | mode_lo; \
        mode_count = 16; \
    } \
    mode_count -= 2; \
    mode = (mode_set >> mode_count) & 0x03;

static int roq_unpack_vq(unsigned char *buf, int size, unsigned int arg, 
    roq_state *state)
{
    int status = ROQ_SUCCESS;
    int mb_x, mb_y;
    int block;     /* 8x8 blocks */
    int subblock;  /* 4x4 blocks */
    int stride = state->stride;
    int i;

    /* frame and pixel management */
    unsigned short *this_frame;
    unsigned short *last_frame;

    int line_offset;
    int mb_offset;
    int block_offset;
    int subblock_offset;

    unsigned short *this_ptr;
    unsigned int *this_ptr32;
    unsigned short *last_ptr;
    /*unsigned int *last_ptr32;*/
    unsigned short *vector16;
    unsigned int *vector32;
    int stride32 = stride / 2;

    /* bytestream management */
    int index = 0;
    int mode_set = 0;
    int mode, mode_lo, mode_hi;
    int mode_count = 0;

    /* vectors */
    int mx, my;
    int motion_x, motion_y;
    unsigned char data_byte;

    mx = (arg >> 8) & 0xFF;
    my =  arg       & 0xFF;

    if (state->current_frame == 1)
    {
        state->current_frame = 0;
        this_frame = state->frame[0];
        last_frame = state->frame[1];
    }
    else
    {
        state->current_frame = 1;
        this_frame = state->frame[1];
        last_frame = state->frame[0];
    }

    for (mb_y = 0; mb_y < state->mb_height && status == ROQ_SUCCESS; mb_y++)
    {
        line_offset = mb_y * 16 * stride;
        for (mb_x = 0; mb_x < state->mb_width && status == ROQ_SUCCESS; mb_x++)
        {
            mb_offset = line_offset + mb_x * 16;
            for (block = 0; block < 4 && status == ROQ_SUCCESS; block++)
            {
                block_offset = mb_offset + state->block_offset_lut[block];
                /* each 8x8 block gets a mode */
                GET_MODE();
                switch (mode)
                {
               case 0:  /* MOT: skip */
                    break;

                case 1:  /* FCC: motion compensation */
                    /* this needs to be done 16 bits at a time due to
                     * data alignment issues on the SH-4 */
                    GET_BYTE(data_byte);
                    motion_x = 8 - (data_byte >>  4) - mx;
                    motion_y = 8 - (data_byte & 0xF) - my;
                    last_ptr = last_frame + block_offset + 
                        (motion_y * stride) + motion_x;
                    this_ptr = this_frame + block_offset;
                    for (i = 0; i < 8; i++)
                    {
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;
                        *this_ptr++ = *last_ptr++;

                        last_ptr += stride - 8;
                        this_ptr += stride - 8;
                    }
                    break;

                case 2:  /* SLD: upsample 4x4 vector */
                    GET_BYTE(data_byte);
                    vector16 = state->cb4x4[data_byte];
                    for (i = 0; i < 4*4; i++)
                    {
                        this_ptr = this_frame + block_offset +
                            state->upsample_offset_lut[i];
                        this_ptr[0] = *vector16;
                        this_ptr[1] = *vector16;
                        this_ptr[stride+0] = *vector16;
                        this_ptr[stride+1] = *vector16;
						vector16++;
                    }
                    break;

    case 3:  /* CCC: subdivide into 4 subblocks */
                    for (subblock = 0; subblock < 4; subblock++)
                    {
                        subblock_offset = block_offset + state->subblock_offset_lut[subblock];

                        GET_MODE();
                        switch (mode)
                        {
                        case 0:  /* MOT: skip */
						break;

                        case 1:  /* FCC: motion compensation */
                            GET_BYTE(data_byte);
                            motion_x = 8 - (data_byte >>  4) - mx;
                            motion_y = 8 - (data_byte & 0xF) - my;
                            last_ptr = last_frame + subblock_offset + 
                                (motion_y * stride) + motion_x;
                            this_ptr = this_frame + subblock_offset;
                            for (i = 0; i < 4; i++)
                            {
                                *this_ptr++ = *last_ptr++;
                                *this_ptr++ = *last_ptr++;
                                *this_ptr++ = *last_ptr++;
                                *this_ptr++ = *last_ptr++;

                                last_ptr += stride - 4;
                                this_ptr += stride - 4;
                            }
                            break;;
                        case 2:  /* SLD: use 4x4 vector from codebook */
                            GET_BYTE(data_byte);
                            vector32 = (unsigned int*)state->cb4x4[data_byte];

                            this_ptr32 = (unsigned int*)this_frame;
                            this_ptr32 += subblock_offset / 2;
                            for (i = 0; i < 4; i++)
                            {
                                *this_ptr32++ = *vector32++;
                                *this_ptr32++ = *vector32++;


                                this_ptr32 += stride32 - 2;
                            }
                            break;

                        case 3:  /* CCC: subdivide into 4 subblocks */
                            GET_BYTE(data_byte);
                            vector16 = state->cb2x2[data_byte];
                            this_ptr = this_frame + subblock_offset;


                            this_ptr[0] = vector16[0];
                            this_ptr[1] = vector16[1];
                            this_ptr[stride+0] = vector16[2];
                            this_ptr[stride+1] = vector16[3];

                            GET_BYTE(data_byte);
                            vector16 = state->cb2x2[data_byte];


                            this_ptr[2] = vector16[0];
                            this_ptr[3] = vector16[1];
                            this_ptr[stride+2] = vector16[2];
                            this_ptr[stride+3] = vector16[3];

                            this_ptr += stride * 2;

                            GET_BYTE(data_byte);
                            vector16 = state->cb2x2[data_byte];


                            this_ptr[0] = vector16[0];
                            this_ptr[1] = vector16[1];
                            this_ptr[stride+0] = vector16[2];
                            this_ptr[stride+1] = vector16[3];

                            GET_BYTE(data_byte);
                            vector16 = state->cb2x2[data_byte];


                            this_ptr[2] = vector16[0];
                            this_ptr[3] = vector16[1];
                            this_ptr[stride+2] = vector16[2];
                            this_ptr[stride+3] = vector16[3];

                            break;
                        }
                    }
                    break;
                }
            }
        }
    }

    /* sanity check to see if the stream was fully consumed */
    if (status == ROQ_SUCCESS && index < size-2)
    {
        status = ROQ_BAD_VQ_STREAM;
    }

    return status;
}
/*
	Name: Ian micheal
	Copyright: 
	Author: 
	Date: 15/08/23 19:24
	Description: ported from C normal file system to kos FS file system api because if this
	
	Info from TapamN One issue you might run into is slow file access over ethernet.
	Using the C library stdio.h functions (fread, fwrite) can be much slower than using the KOS filesystem calls directly (fs_read, fs_write) when reading/writing large blocks.
	With stdio, you get something like tens of KB/sec, while with KOS you can get over 1 MB/sec. Stdio might be faster when preforming many very small operations. 
	dcload-serial doesn't have this issue.
*/

int dreamroq_play(char *filename, int loop, render_callback render_cb,
                  audio_callback audio_cb, quit_callback quit_cb)
{
    file_t f;
    ssize_t file_ret;
    int framerate;
    int chunk_id;
    unsigned int chunk_size;
    unsigned int chunk_arg;
    roq_state state;
    int status;
    int initialized = 0;
    unsigned char read_buffer[MAX_BUF_SIZE];
    int i, snd_left, snd_right;

    f = fs_open(filename, O_RDONLY);
    if (f < 0)
        return ROQ_FILE_OPEN_FAILURE;

    file_ret = fs_read(f, read_buffer, CHUNK_HEADER_SIZE);
    if (file_ret != CHUNK_HEADER_SIZE)
    {
        fs_close(f);
        printf("\nROQ_FILE_READ_FAILURE\n\n");
        return ROQ_FILE_READ_FAILURE;
    }
    framerate = LE_16(&read_buffer[6]);
   printf("RoQ file plays at %d frames/sec\n", framerate);
	

status = ROQ_SUCCESS;
while (1)
{
    if (quit_cb && quit_cb())
        break;

    file_ret = fs_read(f, read_buffer, CHUNK_HEADER_SIZE);
    #ifdef FPSGRAPH
        printf("r\n");
    #endif
    if (file_ret < CHUNK_HEADER_SIZE)
    {
        if (file_ret == 0) // Indicates end of file
            break;
        else if (loop)
        {
            fs_seek(f, 8, SEEK_SET);
            continue;
        }
        else
            break;
    }
    chunk_id = LE_16(&read_buffer[0]);
    chunk_size = LE_32(&read_buffer[2]);
    chunk_arg = LE_16(&read_buffer[6]);

    if (chunk_size > MAX_BUF_SIZE)
    {
        fs_close(f);
        return ROQ_CHUNK_TOO_LARGE;
    }

    file_ret = fs_read(f, read_buffer, chunk_size);
    if (file_ret != chunk_size)
    {
        status = ROQ_FILE_READ_FAILURE;
        break;
    }

            
        switch(chunk_id)
        {
        case RoQ_INFO:
            if (initialized)
                continue;

            state.width = LE_16(&read_buffer[0]);
            state.height = LE_16(&read_buffer[2]);
            /* width and height each need to be divisible by 16 */
            if ((state.width & 0xF) || (state.height & 0xF))
            {
                status = ROQ_INVALID_PIC_SIZE;
                break;
            }
            state.mb_width = state.width / 16;
            state.mb_height = state.height / 16;
            state.mb_count = state.mb_width * state.mb_height;
            if (state.width < 8 || state.width > 1024)
                status = ROQ_INVALID_DIMENSION;
            else
            {
                state.stride = 8;
                while (state.stride < state.width)
                    state.stride <<= 1;
            }

            /* Initialize Audio SQRT Look-Up Table */
            for(i = 0; i < 128; i++)
            {
                roq_audio.snd_sqr_arr[i] = i * i;
                roq_audio.snd_sqr_arr[i + 128] = -(i * i);
            }

            // Initialize YUV420 -> RGB Math Look-Up Table helpers
            for(i = 0; i < 256; i++) {
                state.yy_lut[i] = 1.164 * (i - 16);
                state.cr_r_lut[i] = 1.596 * (i - 128);
                state.cb_b_lut[i] = 2.017 * (i - 128);
                state.cr_g_lut[i] = -0.813 * (i - 128);
                state.cb_g_lut[i] = -0.392 * (i - 128);
            }

            for(i = 0; i < 4; i++) {
                state.block_offset_lut[i] = (i / 2 * 8 * state.stride) + (i % 2 * 8);
            }

            for(i = 0; i < 4; i++) {
                state.subblock_offset_lut[i] = (i / 2 * 4 * state.stride) + (i % 2 * 4);
            }

            for(i = 0; i < 4; i++) {
                state.unpack_4x4_lut[i] = (i / 2) * 8 + (i % 2) * 2;
            }

            for(i = 0; i < 16; i++) {
                state.upsample_offset_lut[i] = (i / 4 * 2 * state.stride) + (i % 4 * 2);
            }

            if (state.height < 8 || state.height > 1024)
                status = ROQ_INVALID_DIMENSION;
            else
            {
                state.texture_height = 8;
                while (state.texture_height < state.height)
                    state.texture_height <<= 1;
            }
            printf("  RoQ_INFO: dimensions = %dx%d, %dx%d; %d mbs, texture = %dx%d\n", 
                state.width, state.height, state.mb_width, state.mb_height,
                state.mb_count, state.stride, state.texture_height);
            state.frame[0] = (unsigned short*)memalign(32, state.texture_height * state.stride * sizeof(unsigned short));
            state.frame[1] = (unsigned short*)memalign(32, state.texture_height * state.stride * sizeof(unsigned short));
            state.current_frame = 0;
            if (!state.frame[0] || !state.frame[1])
            {
                free (state.frame[0]);
                free (state.frame[1]);
                status = ROQ_NO_MEMORY;
                break;
            }
            memset(state.frame[0], 0, state.texture_height * state.stride * sizeof(unsigned short));
            memset(state.frame[1], 0, state.texture_height * state.stride * sizeof(unsigned short));

            /* set this flag so that this code is not executed again when
             * looping */
            initialized = 1;
            break;

        case RoQ_QUAD_CODEBOOK:
            status = roq_unpack_quad_codebook(read_buffer, chunk_size, 
                chunk_arg, &state);
            break;

        case RoQ_QUAD_VQ:
            status = roq_unpack_vq(read_buffer, chunk_size, 
                chunk_arg, &state);
            if (render_cb)
                status = render_cb(state.frame[state.current_frame], 
                    state.width, state.height, state.stride, state.texture_height);
            break;
        case RoQ_SOUND_MONO:
            roq_audio.channels = 1;
            roq_audio.pcm_samples = chunk_size*2;
		    snd_left = chunk_arg;
		    for(i = 0; i < chunk_size; i++)
			{
			    snd_left += roq_audio.snd_sqr_arr[read_buffer[i]];
			    roq_audio.pcm_sample[i * 2] = snd_left & 0xff;
			    roq_audio.pcm_sample[i * 2 + 1] = (snd_left & 0xff00) >> 8;
			}
            if (audio_cb)
                status = audio_cb( roq_audio.pcm_sample, roq_audio.pcm_samples,
                                   roq_audio.channels ); 
            break;

        case RoQ_SOUND_STEREO:
            roq_audio.channels = 2;
            roq_audio.pcm_samples = chunk_size*2;
		    snd_left = (chunk_arg & 0xFF00);
		    snd_right = (chunk_arg & 0xFF) << 8;
		    for(i = 0; i < chunk_size; i += 2)
			{
			    snd_left  += roq_audio.snd_sqr_arr[read_buffer[i]];
			    snd_right += roq_audio.snd_sqr_arr[read_buffer[i+1]];
			    roq_audio.pcm_sample[i * 2] = snd_left & 0xff;
			    roq_audio.pcm_sample[i * 2 + 1] = (snd_left & 0xff00) >> 8;
			    roq_audio.pcm_sample[i * 2 + 2] =  snd_right & 0xff;
			    roq_audio.pcm_sample[i * 2 + 3] = (snd_right & 0xff00) >> 8;
			}
            if (audio_cb)
                status = audio_cb( roq_audio.pcm_sample, roq_audio.pcm_samples,
                                   roq_audio.channels );
            break;

        default:
            break;
    }
}
    free(state.frame[0]);
    free(state.frame[1]);
    fs_close(f);

    return status;
}


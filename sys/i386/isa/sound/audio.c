/*
 * sound/audio.c
 *
 * Device file manager for /dev/audio
 *
 * Copyright by Hannu Savolainen 1993
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * audio.c,v 1.6 1994/10/01 02:16:29 swallace Exp
 */

#include "sound_config.h"

#ifdef CONFIGURE_SOUNDCARD
#ifndef EXCLUDE_AUDIO

#include "ulaw.h"

#define ON		1
#define OFF		0

static int      wr_buff_no[MAX_AUDIO_DEV];	/*
						 * != -1, if there is
						 * a incomplete output
						 * block in the queue.
						 */
static int      wr_buff_size[MAX_AUDIO_DEV], wr_buff_ptr[MAX_AUDIO_DEV];

static int      audio_mode[MAX_AUDIO_DEV];

#define		AM_NONE		0
#define		AM_WRITE	1
#define 	AM_READ		2

static char    *wr_dma_buf[MAX_AUDIO_DEV];
static int      audio_format[MAX_AUDIO_DEV];
static int      local_conversion[MAX_AUDIO_DEV];

static int
set_format (int dev, int fmt)
{
  if (fmt != AFMT_QUERY)
    {

      local_conversion[dev] = 0;

      if (!(audio_devs[dev]->format_mask & fmt))	/* Not supported */
	if (fmt == AFMT_MU_LAW)
	  {
	    fmt = AFMT_U8;
	    local_conversion[dev] = AFMT_MU_LAW;
	  }
	else
	  fmt = AFMT_U8;	/* This is always supported */

      audio_format[dev] = DMAbuf_ioctl (dev, SNDCTL_DSP_SETFMT, fmt, 1);
    }

  if (local_conversion[dev])	/* This shadows the HW format */
    return local_conversion[dev];

  return audio_format[dev];
}

int
audio_open (int dev, struct fileinfo *file)
{
  int             ret;
  int             bits;
  int             dev_type = dev & 0x0f;
  int             mode = file->mode & O_ACCMODE;

  dev = dev >> 4;

  if (dev_type == SND_DEV_DSP16)
    bits = 16;
  else
    bits = 8;

  if ((ret = DMAbuf_open (dev, mode)) < 0)
    return ret;

  local_conversion[dev] = 0;

  if (DMAbuf_ioctl (dev, SNDCTL_DSP_SETFMT, bits, 1) != bits)
    {
      audio_release (dev, file);
      return RET_ERROR (ENXIO);
    }

  if (dev_type == SND_DEV_AUDIO)
    {
      set_format (dev, AFMT_MU_LAW);
    }
  else
    set_format (dev, bits);

  wr_buff_no[dev] = -1;
  audio_mode[dev] = AM_NONE;

  return ret;
}

void
audio_release (int dev, struct fileinfo *file)
{
  int             mode;

  dev = dev >> 4;
  mode = file->mode & O_ACCMODE;

  if (wr_buff_no[dev] >= 0)
    {
      DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev]);

      wr_buff_no[dev] = -1;
    }

  DMAbuf_release (dev, mode);
}

#ifdef NO_INLINE_ASM
static void
translate_bytes (const unsigned char *table, unsigned char *buff, unsigned long n)
{
  unsigned long   i;

  for (i = 0; i < n; ++i)
    buff[i] = table[buff[i]];
}

#else
extern inline void
translate_bytes (const void *table, void *buff, unsigned long n)
{
  __asm__ ("cld\n"
	   "1:\tlodsb\n\t"
	   "xlatb\n\t"
	   "stosb\n\t"
	   "loop 1b\n\t":
	   :"b" ((long) table), "c" (n), "D" ((long) buff), "S" ((long) buff)
	   :"bx", "cx", "di", "si", "ax");
}

#endif

int
audio_write (int dev, struct fileinfo *file, snd_rw_buf * buf, int count)
{
  int             c, p, l;
  int             err;

  dev = dev >> 4;

  p = 0;
  c = count;

  if (audio_mode[dev] == AM_READ)	/*
					 * Direction changed
					 */
    {
      wr_buff_no[dev] = -1;
    }

  audio_mode[dev] = AM_WRITE;

  if (!count)			/*
				 * Flush output
				 */
    {
      if (wr_buff_no[dev] >= 0)
	{
	  DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev]);

	  wr_buff_no[dev] = -1;
	}
      return 0;
    }

  while (c)
    {				/*
				 * Perform output blocking
				 */
      if (wr_buff_no[dev] < 0)	/*
				 * There is no incomplete buffers
				 */
	{
	  if ((wr_buff_no[dev] = DMAbuf_getwrbuffer (dev, &wr_dma_buf[dev], &wr_buff_size[dev])) < 0)
	    {
	      return wr_buff_no[dev];
	    }
	  wr_buff_ptr[dev] = 0;
	}

      l = c;
      if (l > (wr_buff_size[dev] - wr_buff_ptr[dev]))
	l = (wr_buff_size[dev] - wr_buff_ptr[dev]);

      if (!audio_devs[dev]->copy_from_user)
	{			/*
				 * No device specific copy routine
				 */
	  COPY_FROM_USER (&wr_dma_buf[dev][wr_buff_ptr[dev]], buf, p, l);
	}
      else
	audio_devs[dev]->copy_from_user (dev,
			      wr_dma_buf[dev], wr_buff_ptr[dev], buf, p, l);


      /*
       * Insert local processing here
       */

      if (local_conversion[dev] == AFMT_MU_LAW)
	{
#ifdef linux
	  /*
	   * This just allows interrupts while the conversion is running
	   */
	  __asm__ ("sti");
#endif
	  translate_bytes (ulaw_dsp, (unsigned char *) &wr_dma_buf[dev][wr_buff_ptr[dev]], l);
	}

      c -= l;
      p += l;
      wr_buff_ptr[dev] += l;

      if (wr_buff_ptr[dev] >= wr_buff_size[dev])
	{
	  if ((err = DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev])) < 0)
	    {
	      return err;
	    }

	  wr_buff_no[dev] = -1;
	}

    }

  return count;
}

int
audio_read (int dev, struct fileinfo *file, snd_rw_buf * buf, int count)
{
  int             c, p, l;
  char           *dmabuf;
  int             buff_no;

  dev = dev >> 4;
  p = 0;
  c = count;

  if (audio_mode[dev] == AM_WRITE)
    {
      if (wr_buff_no[dev] >= 0)
	{
	  DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev]);

	  wr_buff_no[dev] = -1;
	}
    }

  audio_mode[dev] = AM_READ;

  while (c)
    {
      if ((buff_no = DMAbuf_getrdbuffer (dev, &dmabuf, &l)) < 0)
	return buff_no;

      if (l > c)
	l = c;

      /*
       * Insert any local processing here.
       */

      if (local_conversion[dev] == AFMT_MU_LAW)
	{
#ifdef linux
	  /*
	   * This just allows interrupts while the conversion is running
	   */
	  __asm__ ("sti");
#endif

	  translate_bytes (dsp_ulaw, (unsigned char *) dmabuf, l);
	}

      COPY_TO_USER (buf, p, dmabuf, l);

      DMAbuf_rmchars (dev, buff_no, l);

      p += l;
      c -= l;
    }

  return count - c;
}

int
audio_ioctl (int dev, struct fileinfo *file,
	     unsigned int cmd, unsigned int arg)
{

  dev = dev >> 4;

  switch (cmd)
    {
    case SNDCTL_DSP_SYNC:
      if (wr_buff_no[dev] >= 0)
	{
	  DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev]);

	  wr_buff_no[dev] = -1;
	}
      return DMAbuf_ioctl (dev, cmd, arg, 0);
      break;

    case SNDCTL_DSP_POST:
      if (wr_buff_no[dev] >= 0)
	{
	  DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev]);

	  wr_buff_no[dev] = -1;
	}
      return 0;
      break;

    case SNDCTL_DSP_RESET:
      wr_buff_no[dev] = -1;
      return DMAbuf_ioctl (dev, cmd, arg, 0);
      break;

    case SNDCTL_DSP_GETFMTS:
      return IOCTL_OUT (arg, audio_devs[dev]->format_mask);
      break;

    case SNDCTL_DSP_SETFMT:
      return IOCTL_OUT (arg, set_format (dev, IOCTL_IN (arg)));

    default:
      return DMAbuf_ioctl (dev, cmd, arg, 0);
      break;
    }
}

long
audio_init (long mem_start)
{
  /*
 * NOTE! This routine could be called several times during boot.
 */
  return mem_start;
}

#else
/*
 * Stub versions
 */

int
audio_read (int dev, struct fileinfo *file, snd_rw_buf * buf, int count)
{
  return RET_ERROR (EIO);
}

int
audio_write (int dev, struct fileinfo *file, snd_rw_buf * buf, int count)
{
  return RET_ERROR (EIO);
}

int
audio_open (int dev, struct fileinfo *file)
{
  return RET_ERROR (ENXIO);
}

void
audio_release (int dev, struct fileinfo *file)
{
};
int
audio_ioctl (int dev, struct fileinfo *file,
	     unsigned int cmd, unsigned int arg)
{
  return RET_ERROR (EIO);
}

int
audio_lseek (int dev, struct fileinfo *file, off_t offset, int orig)
{
  return RET_ERROR (EIO);
}

long
audio_init (long mem_start)
{
  return mem_start;
}

#endif

#endif

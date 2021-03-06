/*
 * ZeroCodec Decoder
 *
 * Copyright (c) 2012, Derek Buitenhuis
 *
 * This file is part of FFMpeg.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <zlib.h>

#include "avcodec.h"

typedef struct {
    AVFrame  previous_frame;
    z_stream zstream;
    int size;
} ZeroCodecContext;

static int zerocodec_decode_frame(AVCodecContext *avctx, void *data,
                                  int *data_size, AVPacket *avpkt)
{
    ZeroCodecContext *zc = avctx->priv_data;
    AVFrame *pic         = avctx->coded_frame;
    AVFrame *prev_pic    = &zc->previous_frame;
    z_stream *zstream    = &zc->zstream;
    uint8_t *prev, *dst;
    int i, j, zret;

    pic->reference = 3;

    if (avctx->get_buffer(avctx, pic) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate buffer.\n");
        return AVERROR(ENOMEM);
    }

    zret = inflateReset(zstream);

    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not reset inflate: %d\n", zret);
        return AVERROR(EINVAL);
    }

    zstream->next_in   = avpkt->data;
    zstream->avail_in  = avpkt->size;

    prev = prev_pic->data[0];
    dst  = pic->data[0];

    /**
     * ZeroCodec has very simple interframe compression. If a value
     * is the same as the previous frame, set it to 0.
     */

    if (avpkt->flags & AV_PKT_FLAG_KEY) {

        pic->key_frame = 1;
        pic->pict_type = AV_PICTURE_TYPE_I;

        for (i = 0; i < avctx->height; i++) {

            zstream->next_out  = dst;
            zstream->avail_out = avctx->width << 1;

            zret = inflate(zstream, Z_SYNC_FLUSH);

            if (zret != Z_OK && zret != Z_STREAM_END) {
                av_log(avctx, AV_LOG_ERROR,
                       "Inflate failed with return code: %d\n", zret);
                return AVERROR(EINVAL);
            }

            dst += pic->linesize[0];
        }
    } else {

        pic->key_frame = 0;
        pic->pict_type = AV_PICTURE_TYPE_P;

        for (i = 0; i < avctx->height; i++) {

            zstream->next_out  = dst;
            zstream->avail_out = avctx->width << 1;

            zret = inflate(zstream, Z_SYNC_FLUSH);

            if (zret != Z_OK && zret != Z_STREAM_END) {
                av_log(avctx, AV_LOG_ERROR,
                       "Inflate failed with return code: %d\n", zret);
                return AVERROR(EINVAL);
            }

            for (j = 0; j < avctx->width << 1; j++)
                dst[j] += prev[j] & -!dst[j];

            prev += prev_pic->linesize[0];
            dst  += pic->linesize[0];
        }
    }

    /* Release the previous buffer if need be */
    if (prev_pic->data[0])
        avctx->release_buffer(avctx, prev_pic);

    /* Store the previouse frame for use later */
    *prev_pic = *pic;

    *data_size = sizeof(AVFrame);
    *(AVFrame *)data = *pic;

    return avpkt->size;
}

static av_cold int zerocodec_decode_close(AVCodecContext *avctx)
{
    ZeroCodecContext *zc = avctx->priv_data;
    AVFrame *prev_pic    = &zc->previous_frame;

    inflateEnd(&zc->zstream);

    /* Release last frame */
    if (prev_pic->data[0])
        avctx->release_buffer(avctx, prev_pic);

    av_freep(&avctx->coded_frame);

    return 0;
}

static av_cold int zerocodec_decode_init(AVCodecContext *avctx)
{
    ZeroCodecContext *zc = avctx->priv_data;
    z_stream *zstream    = &zc->zstream;
    int zret;

    avctx->pix_fmt             = PIX_FMT_UYVY422;
    avctx->bits_per_raw_sample = 8;

    zc->size = avpicture_get_size(avctx->pix_fmt,
                                  avctx->width, avctx->height);

    zstream->zalloc = Z_NULL;
    zstream->zfree  = Z_NULL;
    zstream->opaque = Z_NULL;

    zret = inflateInit(zstream);

    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not initialize inflate: %d\n", zret);
        return AVERROR(ENOMEM);
    }

    avctx->coded_frame = avcodec_alloc_frame();

    if (!avctx->coded_frame) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate frame buffer.\n");
        zerocodec_decode_close(avctx);
        return AVERROR(ENOMEM);
    }

    return 0;
}

AVCodec ff_zerocodec_decoder = {
    .type           = AVMEDIA_TYPE_VIDEO,
    .name           = "zerocodec",
    .id             = CODEC_ID_ZEROCODEC,
    .priv_data_size = sizeof(ZeroCodecContext),
    .init           = zerocodec_decode_init,
    .decode         = zerocodec_decode_frame,
    .close          = zerocodec_decode_close,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("ZeroCodec Lossless Video"),
};

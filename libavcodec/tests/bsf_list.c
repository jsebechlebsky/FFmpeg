/*
 * Test utility for bsf list filtering
 *
 * Copyright (c) 2016 Jan Sebechlebsky
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"

static int str2pkt(AVPacket *out, const char * str)
{
    size_t size = strlen(str) + 1;
    uint8_t *data = av_malloc(size);
    memcpy(data, str, size);
    return av_packet_from_data(out, data, size);
}

/* Some BSF filters for testing purposes */

typedef struct TokenizingBSFContext {
    AVClass *class;

    char *delim;
    char *flush_str;
    int flush_nr;

    char *buf, *dup, *saveptr;
} TokenizingBSFContext;

static int tokenizing_bsf_filter(AVBSFContext *bsf, AVPacket *out)
{
    AVPacket pkt;
    TokenizingBSFContext *ctx = bsf->priv_data;
    int ret = 0;
    char *token = NULL;

    if (ctx->dup) {
        token = av_strtok(NULL, ctx->delim, &ctx->saveptr);
        if (token) {
            return str2pkt(out,token);
        } else {
            av_free(ctx->dup);
            ctx->buf = ctx->dup = ctx->saveptr = NULL;
        }
    }

    ret = ff_bsf_get_packet_ref(bsf, &pkt);
    if (ret == AVERROR_EOF && ctx->flush_nr) {
        ret = str2pkt(out, ctx->flush_str);
        ctx->flush_nr--;
        return 0;
    }else if (ret < 0)
        return ret;

    if (!(ctx->buf = ctx->dup = av_strdup(pkt.data)))
        goto end;

    token = av_strtok(ctx->buf, ctx->delim, &ctx->saveptr);
    if (token) {
        ret = str2pkt(out, token);
    } else {
        ret = AVERROR(EAGAIN);
    }

end:
    av_packet_unref(&pkt);
    return ret;
}

static const AVOption tok_bsf_options[] = {
        { "delim", NULL, offsetof(TokenizingBSFContext, delim), AV_OPT_TYPE_STRING, {.str = ","}, 0, 0},
        { "flush_str", NULL, offsetof(TokenizingBSFContext, flush_str), AV_OPT_TYPE_STRING, {.str = "[flush]"}, 0, 0},
        { "flush_nr", NULL, offsetof(TokenizingBSFContext, flush_nr), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 16},
        {NULL},
};

static const AVClass tok_bsf_class = {
        .class_name = "tok",
        .item_name = av_default_item_name,
        .option = tok_bsf_options,
        .version = LIBAVUTIL_VERSION_INT,
};

static const AVBitStreamFilter tok_bsf = {
        .name = "tok",
        .priv_class = &tok_bsf_class,
        .priv_data_size = sizeof(TokenizingBSFContext),
        .filter = tokenizing_bsf_filter,
};

typedef struct ConcatBSFContext {
    AVClass *class;
    int concat_nr;
    int concat_yet;
    AVBPrint bp;
} ConcatBSFContext;


static int concat_output(ConcatBSFContext *ctx, AVPacket *out)
{
    int ret;
    char *str;
    ctx->concat_yet = 0;
    ret = av_bprint_finalize( &ctx->bp, &str);
    if (ret < 0)
        return ret;
    return av_packet_from_data( out, (uint8_t*)str, strlen(str) + 1);
}

static int concat_bsf_filter(AVBSFContext *bsf, AVPacket *out)
{
    ConcatBSFContext *ctx = bsf->priv_data;
    AVPacket pkt;
    int ret = 0;

    av_init_packet(&pkt);

    ret = ff_bsf_get_packet_ref(bsf, &pkt);
    if (ret == AVERROR_EOF)
        return ctx->concat_yet ? concat_output(ctx, out) : AVERROR_EOF;

    if (ret < 0)
        return ret;

    if (!ctx->concat_yet)
        av_bprint_init(&ctx->bp, 16, INT_MAX);

    av_bprintf(&ctx->bp, "%s", pkt.data);
    av_packet_unref(&pkt);
    ctx->concat_yet++;

    return (ctx->concat_yet == ctx->concat_nr) ?
           concat_output(ctx, out) : AVERROR(EAGAIN);
}

static const AVOption concat_bsf_options[] = {
    { "nr", NULL, offsetof(ConcatBSFContext, concat_nr), AV_OPT_TYPE_INT, {.i64 = 2}, 1, 16},
    {NULL},
};

static const AVClass concat_bsf_class = {
    .class_name = "concat",
    .item_name = av_default_item_name,
    .option = concat_bsf_options,
    .version = LIBAVUTIL_VERSION_INT,
};

static const AVBitStreamFilter concat_bsf = {
    .name = "concat",
    .priv_class = &concat_bsf_class,
    .priv_data_size = sizeof(ConcatBSFContext),
    .filter = concat_bsf_filter,
};

static const AVBitStreamFilter* tst_bsf_get_by_name(const char *str)
{
    if (strcmp(str, tok_bsf.name) == 0) {
        return &tok_bsf;
    }else if (strcmp(str, concat_bsf.name) == 0){
        return &concat_bsf;
    }

    return NULL;
}

static int tst_bsf_parse_str(const char *str, AVBSFContext **bsf)
{
    const AVBitStreamFilter *filter;
    char *bsf_name, *bsf_options_str, *buf;
    int ret = 0;

    if(!(buf = av_strdup(str)))
        return AVERROR(ENOMEM);

    bsf_name = av_strtok(buf, "=", &bsf_options_str);
    if (!bsf_name) {
        ret = AVERROR(EINVAL);
        goto end;
    }

    filter = tst_bsf_get_by_name(bsf_name);
    if (!filter) {
        ret = AVERROR_BSF_NOT_FOUND;
        goto end;
    }

    ret = av_bsf_alloc(filter, bsf);
    if (ret < 0)
        goto end;

    if (bsf_options_str) {
        ret = av_set_options_string(*bsf, bsf_options_str, "=", ":");
        if (ret < 0)
            av_bsf_free(bsf);
    }

end:
    av_free(buf);
    return ret;
}

static int tst_bsf_list_parse_str(const char *str, AVBSFContext **bsf_lst)
{
    AVBSFList *lst;
    char *bsf_str, *buf, *dup, *saveptr;
    int ret;

    if (!str)
        return av_bsf_get_null_filter(bsf_lst);

    lst = av_bsf_list_alloc();
    if (!lst)
        return AVERROR(ENOMEM);

    if (!(dup = buf = av_strdup(str)))
        return AVERROR(ENOMEM);

    while (1) {
        AVBSFContext *bsf;
        bsf_str = av_strtok(buf, ",", &saveptr);
        if (!bsf_str)
            break;

        ret = tst_bsf_parse_str(bsf_str, &bsf);
        if (ret < 0)
            goto end;

        ret = av_bsf_list_append(lst, bsf);
        if (ret < 0) {
            av_bsf_free(&bsf);
            goto end;
        }

        buf = NULL;
    }

    ret = av_bsf_list_finalize(&lst, bsf_lst);
end:
    if (ret < 0)
        av_bsf_list_free(&lst);
    av_free(dup);
    return ret;
}

static int filter_packet(AVBSFContext *bsf_list, AVPacket *in)
{
    int ret;
    AVPacket pkt;
    av_init_packet(&pkt);

    ret = av_bsf_send_packet(bsf_list, in);
    if (ret < 0) {
        printf("Failed to send packet to bsf list: %s\n", av_err2str(ret));
        return ret;
    }

    do {
        ret = av_bsf_receive_packet(bsf_list, &pkt);
        if (!ret) {
            printf("\"%s\",", (char *)pkt.data);
            av_packet_unref(&pkt);
        }
    } while (!ret);

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        ret = 0;
    }

    return ret;
}

static void test_list_filtering(const char *bsf_list_str, const char * to_filter[])
{
    int ret = 0, i;
    AVBSFContext *bsf_list = NULL;
    AVPacket pkt;

    printf("bsfs: '%s' input: ", bsf_list_str);
    for (i = 0; to_filter[i]; i++) {
        printf(i ? ",'%s'":"'%s'", to_filter[i]);
    }
    printf("\n    -> ");

    ret = tst_bsf_list_parse_str(bsf_list_str, &bsf_list);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to parse list of bitstream filters: \"%s\"",
               bsf_list_str);
        return;
    }

    ret = av_bsf_init(bsf_list);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to initialize list of bitstream filters: %s\n",
               av_err2str(ret));
        goto end;
    }

    av_init_packet(&pkt);
    for ( i = 0; to_filter[i]; i++) {
        ret = str2pkt(&pkt, to_filter[i]);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error constructing test packet: %s\n",
                   av_err2str(ret));
            goto end;
        }

        ret = filter_packet(bsf_list, &pkt);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error filtering packet: %s\n",
                   av_err2str(ret));
            goto end;
        }
    }

    ret = filter_packet(bsf_list, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error flushing list of bitstream filters: %s\n",
               av_err2str(ret));
        goto end;
    }

end:
    av_bsf_free(&bsf_list);
    printf("\n");
}

static const char *tst1_pkts[] = {"a,b,c", "d,e,f", "g,h,i", NULL};
static const char *tst1_bsfs = "tok,concat";

static const char *tst2_pkts[] = {"a?b!c", "d?e!f", "g?h!i", NULL};
static const char *tst2_bsfs = "tok=delim=?:flush_str=x!x!x:flush_nr=3,tok=delim=!";

static const char *tst3_pkts[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", NULL};
static const char *tst3_bsfs = "concat,concat=nr=3";

static const char *tst4_pkts[] = {"a-b,c-d", "e-f,g-h", "i-j,k-l", "m-n,o-p", NULL};
static const char *tst4_bsfs = "tok,concat,tok=delim=-";


int main(int argc, char *argv[])
{
    test_list_filtering(tst1_bsfs, tst1_pkts);
    test_list_filtering(tst2_bsfs, tst2_pkts);
    test_list_filtering(tst3_bsfs, tst3_pkts);
    test_list_filtering(tst4_bsfs, tst4_pkts);
    return 0;
}

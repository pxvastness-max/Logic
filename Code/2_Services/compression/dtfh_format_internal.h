/**
 * @file   dtfh_format_internal.h
 * @brief  DTFH 2.0文件布局与1.0读取兼容常量
 */

#ifndef DTFH_FORMAT_INTERNAL_H
#define DTFH_FORMAT_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#define DTFH_VERSION_MAJOR             2U
#define DTFH_VERSION_MINOR             0U
#define DTFH_LEGACY_VERSION_MAJOR      1U /* 只读取1.0 */
#define DTFH_FIXED_HEADER_BYTES        64U
#define DTFH_DIRECTORY_BYTES           48U
#define DTFH_BASE_HEADER_BYTES         (DTFH_FIXED_HEADER_BYTES + 6U * DTFH_DIRECTORY_BYTES)
#define DTFH_CODEBOOK_BYTES            (5U * 256U)
#define DTFH_MAX_HEADER_BYTES          (DTFH_BASE_HEADER_BYTES + DTFH_CODEBOOK_BYTES)
#define DTFH_FOOTER_BYTES              16U
#define DTFH_FLAG_COMPLETE             1U
#define DTFH_FLAG_HUFFMAN              2U
#define DTFH_FLAG_SPARSE_CODEBOOK      4U
#define DTFH_MIN_SPARSE_CODEBOOK_BYTES (5U * 2U)
#define DTFH_METADATA_CRC_OFFSET       56U

/* ================================================== */

/* 文件字段顺序与宽度，所有整数字段采用小端序，禁止直接写入 C 结构体 */

#ifdef __cplusplus
}
#endif

#endif /* DTFH_FORMAT_INTERNAL_H */

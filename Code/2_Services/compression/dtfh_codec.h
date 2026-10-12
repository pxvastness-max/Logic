/**
 * @file   dtfh_codec.h
 * @brief  DTFH差值压缩基础算法
 */

#ifndef DTFH_CODEC_H
#define DTFH_CODEC_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stdint.h"

/* ================================================== */

#define DTFH_CODEC_TABLE_COUNT          5U
#define DTFH_CODEC_SYMBOL_COUNT         256U
#define DTFH_CODEC_MAX_CODE_BITS        15U
#define DTFH_CODEC_HUFFMAN_NODE_COUNT   (2U * DTFH_CODEC_SYMBOL_COUNT - 1U)
#define DTFH_CODEC_ULEB128_MAX_BYTES    5U
#define DTFH_CODEC_CRC32_INIT           UINT32_MAX
#define DTFH_CODEC_DENSE_CODEBOOK_BYTES (DTFH_CODEC_TABLE_COUNT * DTFH_CODEC_SYMBOL_COUNT)
#define DTFH_CODEC_SPARSE_COUNT_BYTES   (DTFH_CODEC_TABLE_COUNT * 2U)

/* ================================================== */

typedef enum
{
    DTFH_CODEC_OK = 0,
    DTFH_CODEC_ERROR_INVALID_PARAMETER,
    DTFH_CODEC_ERROR_DATA,
    DTFH_CODEC_ERROR_IO
} DTFH_Codec_Result_t;

typedef struct
{
    uint8_t length[DTFH_CODEC_SYMBOL_COUNT];              /* 0表示符号没有编码 */
    uint16_t code[DTFH_CODEC_SYMBOL_COUNT];               /* 规范码，写出时高位先行 */
    uint8_t symbol[DTFH_CODEC_SYMBOL_COUNT];              /* 按码长、符号值排序 */
    uint16_t first_code[DTFH_CODEC_MAX_CODE_BITS + 1U];   /* 每种码长的最小规范码，按码长索引 */
    uint16_t first_symbol[DTFH_CODEC_MAX_CODE_BITS + 1U]; /* 每种码长在排序符号数组中的起点 */
    uint16_t count[DTFH_CODEC_MAX_CODE_BITS + 1U];        /* 每种码长包含的符号数量 */
    uint16_t symbol_count;                                /* 有非零码长的有效符号数量 0..256 */
} DTFH_Codec_Huffman_Table_t;

typedef struct
{
    uint32_t weight[DTFH_CODEC_HUFFMAN_NODE_COUNT]; /* 叶子和内部节点的合并频率，构造期间使用 */
    uint16_t parent[DTFH_CODEC_HUFFMAN_NODE_COUNT]; /* 各节点父下标，UINT16_MAX 表示尚无父节点 */
} DTFH_Codec_Huffman_Builder_t;

/* 位回调返回1表示成功、0表示流读写失败 */
typedef uint8_t (*DTFH_Codec_Write_Bit_t)(void *context, uint8_t bit);
typedef uint8_t (*DTFH_Codec_Read_Bit_t)(void *context, uint8_t *bit);

/* ================================================== */

/**
 * @brief  将 uint32_t 编码为最短 ULEB128 字节序列
 * @param  bytes 调用者提供的 5 字节输出缓冲
 * @retval 1..5 实际编码字节数，0 也编码为单个零字节
 */
uint8_t DTFH_Codec_ULEB128_Encode(uint32_t value,
                               uint8_t bytes[DTFH_CODEC_ULEB128_MAX_BYTES]);

/**
 * @brief  严格解码恰好一个完整的最短 ULEB128 值
 * @param  bytes 输入编码缓冲，至少包含 count 个字节
 * @retval DTFH_CODEC_OK 解码成功，输出已写入
 */
DTFH_Codec_Result_t DTFH_Codec_ULEB128_Decode(const uint8_t *bytes,
                                           uint8_t count,
                                           uint32_t *value);

/**
 * @brief  按任意分块更新 CRC-32/ISO-HDLC 累计值
 * @param  data 本次原始字节缓冲
 * @retval 尚未进行最终异或的 CRC 累计值
 * @note   data 为 NULL 时直接返回 crc，调用者须先独立验证必需的输入地址
 */
uint32_t DTFH_Codec_CRC32_Update(uint32_t crc, const void *data, uint32_t length);

/**
 * @brief  对 CRC 累计值执行最终反转
 * @retval 可写入文件或用于比较的 CRC32 值
 */
uint32_t DTFH_Codec_CRC32_Finish(uint32_t crc);

/**
 * @brief  用符号频率构建最多 15 位的规范 Huffman 表
 * @param  frequency 256 个符号的非负频率数组
 * @retval DTFH_CODEC_OK 表构建成功，空表和单符号表均合法
 */
DTFH_Codec_Result_t DTFH_Codec_Huffman_Build(
                        const uint32_t frequency[DTFH_CODEC_SYMBOL_COUNT],
                        DTFH_Codec_Huffman_Table_t *table,
                        DTFH_Codec_Huffman_Builder_t *builder);

/**
 * @brief  验证码长并恢复规范 Huffman 编码与解码表
 * @retval DTFH_CODEC_OK 恢复成功
 */
DTFH_Codec_Result_t DTFH_Codec_Huffman_Restore(
                        const uint8_t length[DTFH_CODEC_SYMBOL_COUNT],
                        DTFH_Codec_Huffman_Table_t *table);

/**
 * @brief  为五张共享码表选择总字节数较小的文件表示
 * @param  sparse 输出格式：0 表示保存全部码长，1 表示只保存已用符号及其码长
 * @retval 非零 选定表示的实际字节数，相等时选择 1280 字节定长表
 */
uint16_t DTFH_Codec_Codebook_Select(
                        const DTFH_Codec_Huffman_Table_t tables[DTFH_CODEC_TABLE_COUNT],
                        uint8_t *sparse);

/**
 * @brief  把五张编码表写入字节缓冲区
 * @param  data 调用者独立输出缓冲
 * @retval DTFH_CODEC_OK 全部码表写出完成
 */
DTFH_Codec_Result_t DTFH_Codec_Codebook_Encode(
                        const DTFH_Codec_Huffman_Table_t tables[DTFH_CODEC_TABLE_COUNT],
                        uint8_t sparse, uint8_t *data, uint16_t capacity,
                        uint16_t *written);

/**
 * @brief  从字节缓冲区读出五张编码表并检查内容
 * @param  tables 非 NULL 的五张表输出
 * @retval DTFH_CODEC_OK 五张表全部恢复成功
 */
DTFH_Codec_Result_t DTFH_Codec_Codebook_Decode(
                        const uint8_t *data, uint16_t bytes, uint8_t sparse,
                        DTFH_Codec_Huffman_Table_t tables[DTFH_CODEC_TABLE_COUNT]);

/**
 * @brief  通过位写回调输出一个已编码符号
 * @retval DTFH_CODEC_OK 符号的全部码位已经输出
 */
DTFH_Codec_Result_t DTFH_Codec_Huffman_Write(
                        const DTFH_Codec_Huffman_Table_t *table,
                        uint8_t symbol,
                        DTFH_Codec_Write_Bit_t write_bit,
                        void *context);

/**
 * @brief  通过位读回调解码一个规范 Huffman 符号
 * @param  read_bit 非 NULL 的回调，返回 1 并输出 0/1 表示成功
 * @retval DTFH_CODEC_OK 解码成功
 */
DTFH_Codec_Result_t DTFH_Codec_Huffman_Read(
                        const DTFH_Codec_Huffman_Table_t *table,
                        DTFH_Codec_Read_Bit_t read_bit,
                        void *context,
                        uint8_t *symbol);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* DTFH_CODEC_H */

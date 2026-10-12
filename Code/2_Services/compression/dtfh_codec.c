/**
 * @file   dtfh_codec.c
 * @brief  ULEB128、规范Huffman和CRC32实现
 */

/* ================================================== */

#include "compression/dtfh_codec.h"
#include "stddef.h"
#include "string.h"

/* ================================================== */

#define DTFH_CODEC_NODE_NONE  UINT16_MAX
#define DTFH_CODEC_CRC32_POLY 0xEDB88320UL

/* ================================================== */

/**
 * @brief  将32位无符号数编码为最短ULEB128
 */
uint8_t DTFH_Codec_ULEB128_Encode(uint32_t value,
                               uint8_t bytes[DTFH_CODEC_ULEB128_MAX_BYTES])
{
    uint8_t count = 0U;

    if (bytes == NULL)
    {
        return 0U;
    }

    do
    {
        uint8_t byte = (uint8_t)(value & 0x7FU);

        value >>= 7U;
        if (value != 0U)
        {
            byte |= 0x80U;
        }
        bytes[count++] = byte;
    } while (value != 0U);

    return count;
}

/**
 * @brief  严格解码一个完整ULEB128值
 */
DTFH_Codec_Result_t DTFH_Codec_ULEB128_Decode(const uint8_t *bytes,
                                           uint8_t count,
                                           uint32_t *value)
{
    uint32_t decoded = 0U;

    if ((bytes == NULL) || (value == NULL))
    {
        return DTFH_CODEC_ERROR_INVALID_PARAMETER;
    }
    if ((count == 0U) || (count > DTFH_CODEC_ULEB128_MAX_BYTES))
    {
        return DTFH_CODEC_ERROR_DATA;
    }

    for (uint8_t index = 0U; index < count; index++)
    {
        uint8_t byte = bytes[index];
        uint8_t last = (uint8_t)(index == (uint8_t)(count - 1U));

        if (((byte & 0x80U) != 0U) == (last != 0U))
        {
            return DTFH_CODEC_ERROR_DATA;
        }
        if ((index == 4U) && ((byte & 0xF0U) != 0U))
        {
            return DTFH_CODEC_ERROR_DATA;
        }
        if ((last != 0U) && (count > 1U) && ((byte & 0x7FU) == 0U))
        {
            return DTFH_CODEC_ERROR_DATA;
        }
        decoded |= (uint32_t)(byte & 0x7FU) << (7U * index);
    }

    *value = decoded;
    return DTFH_CODEC_OK;
}

/* ================================================== */

/**
 * @brief  增量更新CRC-32/ISO-HDLC
 */
uint32_t DTFH_Codec_CRC32_Update(uint32_t crc, const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;

    if (bytes == NULL)
    {
        return crc;
    }

    for (uint32_t index = 0U; index < length; index++)
    {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            uint32_t mask = 0U - (crc & 1U);

            crc = (crc >> 1U) ^ (DTFH_CODEC_CRC32_POLY & mask);
        }
    }

    return crc;
}

/**
 * @brief  执行CRC32最终异或
 */
uint32_t DTFH_Codec_CRC32_Finish(uint32_t crc)
{
    return crc ^ UINT32_MAX;
}

/* ================================================== */

/**
 * @brief  验证码长并构造规范Huffman编码、解码表
 */
DTFH_Codec_Result_t DTFH_Codec_Huffman_Restore(
                        const uint8_t length[DTFH_CODEC_SYMBOL_COUNT],
                        DTFH_Codec_Huffman_Table_t *table)
{
    uint32_t slots = 1U;
    uint32_t next_code = 0U;
    uint16_t next_symbol = 0U;

    if ((length == NULL) || (table == NULL))
    {
        return DTFH_CODEC_ERROR_INVALID_PARAMETER;
    }

    /* 先复制码长，允许调用者直接恢复table内已有的码长 */
    memmove(table->length, length, sizeof(table->length));
    memset(table->code, 0, sizeof(table->code));
    memset(table->symbol, 0, sizeof(table->symbol));
    memset(table->first_code, 0, sizeof(table->first_code));
    memset(table->first_symbol, 0, sizeof(table->first_symbol));
    memset(table->count, 0, sizeof(table->count));
    table->symbol_count = 0U;

    for (uint16_t symbol = 0U; symbol < DTFH_CODEC_SYMBOL_COUNT; symbol++)
    {
        uint8_t bits = table->length[symbol];

        if (bits > DTFH_CODEC_MAX_CODE_BITS)
        {
            return DTFH_CODEC_ERROR_DATA;
        }
        if (bits != 0U)
        {
            table->count[bits]++;
            table->symbol_count++;
        }
    }
    if (table->symbol_count == 0U)
    {
        return DTFH_CODEC_OK;
    }

    for (uint8_t bits = 1U; bits <= DTFH_CODEC_MAX_CODE_BITS; bits++)
    {
        slots <<= 1U;
        if ((uint32_t)table->count[bits] > slots)
        {
            return DTFH_CODEC_ERROR_DATA;
        }
        slots -= table->count[bits];
    }
    if (table->symbol_count == 1U)
    {
        if (table->count[1U] != 1U)
        {
            return DTFH_CODEC_ERROR_DATA;
        }
    }
    else if (slots != 0U)
    {
        return DTFH_CODEC_ERROR_DATA;
    }

    for (uint8_t bits = 1U; bits <= DTFH_CODEC_MAX_CODE_BITS; bits++)
    {
        next_code = (next_code + table->count[bits - 1U]) << 1U;
        table->first_code[bits] = (uint16_t)next_code;
        table->first_symbol[bits] = next_symbol;

        for (uint16_t symbol = 0U; symbol < DTFH_CODEC_SYMBOL_COUNT; symbol++)
        {
            if (table->length[symbol] == bits)
            {
                table->code[symbol] = (uint16_t)(next_code +
                        next_symbol - table->first_symbol[bits]);
                table->symbol[next_symbol++] = (uint8_t)symbol;
            }
        }
    }

    return DTFH_CODEC_OK;
}

/**
 * @brief  从尚未合并的节点中取出权重最小节点
 */
static uint16_t DTFH_Codec_Huffman_Minimum(
                        const DTFH_Codec_Huffman_Builder_t *builder,
                        uint16_t node_count,
                        uint16_t excluded)
{
    uint16_t minimum = DTFH_CODEC_NODE_NONE;

    for (uint16_t node = 0U; node < node_count; node++)
    {
        if ((node != excluded) &&
            (builder->weight[node] != 0U) &&
            (builder->parent[node] == DTFH_CODEC_NODE_NONE) &&
            ((minimum == DTFH_CODEC_NODE_NONE) ||
             (builder->weight[node] < builder->weight[minimum])))
        {
            minimum = node;
        }
    }

    return minimum;
}

/**
 * @brief  由频率构造确定的Huffman码长
 */
DTFH_Codec_Result_t DTFH_Codec_Huffman_Build(
                        const uint32_t frequency[DTFH_CODEC_SYMBOL_COUNT],
                        DTFH_Codec_Huffman_Table_t *table,
                        DTFH_Codec_Huffman_Builder_t *builder)
{
    uint64_t total = 0U;
    uint16_t used = 0U;
    uint16_t node_count = DTFH_CODEC_SYMBOL_COUNT;
    uint8_t fallback = 0U;

    if ((frequency == NULL) || (table == NULL) || (builder == NULL))
    {
        return DTFH_CODEC_ERROR_INVALID_PARAMETER;
    }

    memset(table->length, 0, sizeof(table->length));
    for (uint16_t node = 0U; node < DTFH_CODEC_HUFFMAN_NODE_COUNT; node++)
    {
        builder->weight[node] = 0U;
        builder->parent[node] = DTFH_CODEC_NODE_NONE;
    }
    for (uint16_t symbol = 0U; symbol < DTFH_CODEC_SYMBOL_COUNT; symbol++)
    {
        builder->weight[symbol] = frequency[symbol];
        total += frequency[symbol];
        if (frequency[symbol] != 0U)
        {
            used++;
        }
    }
    if (total > UINT32_MAX)
    {
        return DTFH_CODEC_ERROR_INVALID_PARAMETER;
    }
    if (used == 1U)
    {
        table->length[DTFH_Codec_Huffman_Minimum(builder, node_count,
                                   DTFH_CODEC_NODE_NONE)] = 1U;
    }
    else if (used > 1U)
    {
        for (uint16_t merge = 1U; merge < used; merge++)
        {
            uint16_t left = DTFH_Codec_Huffman_Minimum(builder, node_count,
                                                      DTFH_CODEC_NODE_NONE);
            uint16_t right = DTFH_Codec_Huffman_Minimum(builder, node_count, left);

            builder->parent[left] = node_count;
            builder->parent[right] = node_count;
            builder->weight[node_count] = builder->weight[left] +
                                          builder->weight[right];
            node_count++;
        }
        for (uint16_t symbol = 0U; symbol < DTFH_CODEC_SYMBOL_COUNT; symbol++)
        {
            uint16_t node = symbol;
            uint8_t bits = 0U;

            if (frequency[symbol] == 0U)
            {
                continue;
            }
            while (builder->parent[node] != DTFH_CODEC_NODE_NONE)
            {
                bits++;
                node = builder->parent[node];
                if (bits > DTFH_CODEC_MAX_CODE_BITS)
                {
                    fallback = 1U;
                    break;
                }
            }
            table->length[symbol] = bits;
        }
    }

    if (fallback != 0U)
    {
        /* 固定8位是完整合法码树，不截断任何原Huffman码字 */
        memset(table->length, 8, sizeof(table->length));
    }

    return DTFH_Codec_Huffman_Restore(table->length, table);
}

/* ================================================== */

/**
 * @brief  计算只保存已用符号时，五张编码表需要多少字节
 */
static uint16_t DTFH_Codec_Codebook_Sparse_Bytes(
                        const DTFH_Codec_Huffman_Table_t *tables)
{
    uint16_t bytes = DTFH_CODEC_SPARSE_COUNT_BYTES;

    for (uint8_t position = 0U; position < DTFH_CODEC_TABLE_COUNT; position++)
    {
        for (uint16_t symbol = 0U; symbol < DTFH_CODEC_SYMBOL_COUNT; symbol++)
        {
            if (tables[position].length[symbol] != 0U)
            {
                bytes += 2U;
            }
        }
    }
    return bytes;
}

/**
 * @brief  整组码表按实际字节数选择表示，相等时采用定长表
 */
uint16_t DTFH_Codec_Codebook_Select(
                        const DTFH_Codec_Huffman_Table_t tables[DTFH_CODEC_TABLE_COUNT],
                        uint8_t *sparse)
{
    uint16_t bytes;

    if ((tables == NULL) || (sparse == NULL))
    {
        return 0U;
    }
    bytes = DTFH_Codec_Codebook_Sparse_Bytes(tables);
    *sparse = (bytes < DTFH_CODEC_DENSE_CODEBOOK_BYTES) ? 1U : 0U;
    return (*sparse != 0U) ? bytes : DTFH_CODEC_DENSE_CODEBOOK_BYTES;
}

/**
 * @brief  把五张编码表写入字节缓冲区，不额外申请内存
 */
DTFH_Codec_Result_t DTFH_Codec_Codebook_Encode(
                        const DTFH_Codec_Huffman_Table_t tables[DTFH_CODEC_TABLE_COUNT],
                        uint8_t sparse, uint8_t *data, uint16_t capacity,
                        uint16_t *written)
{
    uint16_t bytes;
    uint16_t offset = 0U;

    if (written != NULL)
    {
        *written = 0U;
    }
    if ((tables == NULL) || (data == NULL) || (written == NULL) || (sparse > 1U))
    {
        return DTFH_CODEC_ERROR_INVALID_PARAMETER;
    }
    bytes = (sparse != 0U) ? DTFH_Codec_Codebook_Sparse_Bytes(tables) :
                            DTFH_CODEC_DENSE_CODEBOOK_BYTES;
    if (bytes > capacity)
    {
        return DTFH_CODEC_ERROR_DATA;
    }
    for (uint8_t position = 0U; position < DTFH_CODEC_TABLE_COUNT; position++)
    {
        uint16_t count = 0U;

        for (uint16_t symbol = 0U; symbol < DTFH_CODEC_SYMBOL_COUNT; symbol++)
        {
            uint8_t length = tables[position].length[symbol];

            if (length > DTFH_CODEC_MAX_CODE_BITS)
            {
                return DTFH_CODEC_ERROR_DATA;
            }
            if (length != 0U)
            {
                count++;
            }
        }
        if (sparse == 0U)
        {
            memcpy(data + offset, tables[position].length, DTFH_CODEC_SYMBOL_COUNT);
            offset += DTFH_CODEC_SYMBOL_COUNT;
        }
        else
        {
            data[offset++] = (uint8_t)count;
            data[offset++] = (uint8_t)(count >> 8U);
            for (uint16_t symbol = 0U; symbol < DTFH_CODEC_SYMBOL_COUNT; symbol++)
            {
                if (tables[position].length[symbol] != 0U)
                {
                    data[offset++] = (uint8_t)symbol;
                    data[offset++] = tables[position].length[symbol];
                }
            }
        }
    }
    *written = offset;
    return DTFH_CODEC_OK;
}

/**
 * @brief  从字节缓冲区恢复五张编码表，并检查长度和编码是否有效
 */
DTFH_Codec_Result_t DTFH_Codec_Codebook_Decode(
                        const uint8_t *data, uint16_t bytes, uint8_t sparse,
                        DTFH_Codec_Huffman_Table_t tables[DTFH_CODEC_TABLE_COUNT])
{
    uint16_t offset = 0U;

    if ((data == NULL) || (tables == NULL) || (sparse > 1U))
    {
        return DTFH_CODEC_ERROR_INVALID_PARAMETER;
    }
    if ((sparse == 0U) && (bytes != DTFH_CODEC_DENSE_CODEBOOK_BYTES))
    {
        return DTFH_CODEC_ERROR_DATA;
    }
    for (uint8_t position = 0U; position < DTFH_CODEC_TABLE_COUNT; position++)
    {
        if (sparse == 0U)
        {
            memcpy(tables[position].length, data + offset, DTFH_CODEC_SYMBOL_COUNT);
            offset += DTFH_CODEC_SYMBOL_COUNT;
        }
        else
        {
            uint16_t count;
            uint8_t previous = 0U;

            if ((uint32_t)offset + 2U > bytes)
            {
                return DTFH_CODEC_ERROR_DATA;
            }
            count = (uint16_t)((uint16_t)data[offset] |
                               ((uint16_t)data[offset + 1U] << 8U));
            offset += 2U;
            if ((count > DTFH_CODEC_SYMBOL_COUNT) ||
                ((uint32_t)offset + 2U * count > bytes))
            {
                return DTFH_CODEC_ERROR_DATA;
            }
            memset(tables[position].length, 0, sizeof(tables[position].length));
            for (uint16_t index = 0U; index < count; index++)
            {
                uint8_t symbol = data[offset++];
                uint8_t length = data[offset++];

                if (((index != 0U) && (symbol <= previous)) ||
                    (length == 0U) || (length > DTFH_CODEC_MAX_CODE_BITS))
                {
                    return DTFH_CODEC_ERROR_DATA;
                }
                tables[position].length[symbol] = length;
                previous = symbol;
            }
        }
        if (DTFH_Codec_Huffman_Restore(tables[position].length,
                                       &tables[position]) != DTFH_CODEC_OK)
        {
            return DTFH_CODEC_ERROR_DATA;
        }
    }
    return (offset == bytes) ? DTFH_CODEC_OK : DTFH_CODEC_ERROR_DATA;
}

/* ================================================== */

/**
 * @brief  通过位回调写出一个符号
 */
DTFH_Codec_Result_t DTFH_Codec_Huffman_Write(
                        const DTFH_Codec_Huffman_Table_t *table,
                        uint8_t symbol,
                        DTFH_Codec_Write_Bit_t write_bit,
                        void *context)
{
    uint8_t bits;

    if ((table == NULL) || (write_bit == NULL))
    {
        return DTFH_CODEC_ERROR_INVALID_PARAMETER;
    }
    bits = table->length[symbol];
    if ((bits == 0U) || (bits > DTFH_CODEC_MAX_CODE_BITS))
    {
        return DTFH_CODEC_ERROR_DATA;
    }
    while (bits != 0U)
    {
        bits--;
        if (write_bit(context, (uint8_t)((table->code[symbol] >> bits) & 1U)) == 0U)
        {
            return DTFH_CODEC_ERROR_IO;
        }
    }

    return DTFH_CODEC_OK;
}

/**
 * @brief  通过位回调解码一个符号
 */
DTFH_Codec_Result_t DTFH_Codec_Huffman_Read(
                        const DTFH_Codec_Huffman_Table_t *table,
                        DTFH_Codec_Read_Bit_t read_bit,
                        void *context,
                        uint8_t *symbol)
{
    uint32_t code = 0U;

    if ((table == NULL) || (read_bit == NULL) || (symbol == NULL))
    {
        return DTFH_CODEC_ERROR_INVALID_PARAMETER;
    }
    if (table->symbol_count == 0U)
    {
        return DTFH_CODEC_ERROR_DATA;
    }

    for (uint8_t bits = 1U; bits <= DTFH_CODEC_MAX_CODE_BITS; bits++)
    {
        uint8_t bit;
        uint32_t offset;

        if (read_bit(context, &bit) == 0U)
        {
            return DTFH_CODEC_ERROR_IO;
        }
        if (bit > 1U)
        {
            return DTFH_CODEC_ERROR_DATA;
        }
        code = (code << 1U) | bit;
        offset = code - table->first_code[bits];
        if (offset < table->count[bits])
        {
            *symbol = table->symbol[table->first_symbol[bits] + offset];
            return DTFH_CODEC_OK;
        }
        if (table->symbol_count == 1U)
        {
            return DTFH_CODEC_ERROR_DATA;
        }
    }

    return DTFH_CODEC_ERROR_DATA;
}

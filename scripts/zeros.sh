# 设置总的 LBA 数量
TOTAL_LBA=$(blockdev --getsz /dev/sfd0n1)

# 使用较小的批次大小（例如每次 65535 个块，这是 16 位的最大值）
BATCH_SIZE=65535

# 分批执行 write-zeroes with deallocate
START=0
while [ $START -lt $TOTAL_LBA ]; do
    REMAINING=$((TOTAL_LBA - START))
    if [ $REMAINING -gt $BATCH_SIZE ]; then
        COUNT=$BATCH_SIZE
    else
        COUNT=$REMAINING
    fi
    
    echo "Deallocating blocks $START to $((START + COUNT - 1))"
    sudo nvme write-zeroes /dev/sfd0n1 -s $START -c $COUNT -d
    
    START=$((START + COUNT))
done
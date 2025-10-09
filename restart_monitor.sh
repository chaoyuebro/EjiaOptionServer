#!/bin/bash

# --- 脚本配置 ---
# 目标时间点 (HH:MM 格式)，脚本只在此时间执行
TARGET_TIME="20:35"

# 用于 pgrep -f 查找进程的模式。
# 使用以 "$ " 结尾的模式，可以精确匹配以该名称结尾的进程，
# 既能匹配 './EjiaOptionServer.out' 又能匹配 '/path/to/EjiaOptionServer.out'
# 同时避免误杀 'vi EjiaOptionServer.out' 等进程。
PGREP_PATTERN="/EjiaOptionServer.out$"

# 杀死进程后的等待时间（秒），为外部监控提供稳定的反应窗口
KILL_SLEEP_DURATION=60

# CPU检测相关配置
CPU_CHECK_INTERVAL=300  # 5分钟 = 300秒
CPU_CHECK_DURATION=30   # CPU检测持续时间（秒）
PROCESS_CHECK_INTERVAL=60  # 进程存在检测间隔（秒）

# --- 脚本函数 (无日志输出) ---
kill_process() {
    # 使用新的、更可靠的模式来查找进程ID
    PID=$(pgrep -f "$PGREP_PATTERN")
    if [ -n "$PID" ]; then
        # 1. 尝试发送正常的终止信号 (SIGTERM)
        kill "$PID"
        # 2. 等待一段较长的时间
        sleep "$KILL_SLEEP_DURATION"
        # 3. 再次检查原PID是否还存在，如果还在，就强制杀死
        if ps -p "$PID" > /dev/null; then
            kill -9 "$PID"
        fi
    fi
}

# 检测进程CPU占用率
check_cpu_usage() {
    PID=$(pgrep -f "$PGREP_PATTERN")
    if [ -z "$PID" ]; then
        return 1  # 进程不存在
    fi
    
    # 获取系统CPU核心数，计算动态阈值（核心数 × 50%）
    CPU_CORES=$(nproc)
    ACTUAL_THRESHOLD=$((CPU_CORES * 50))
    
    # 使用top命令获取进程CPU占用率，检测30秒
    local high_cpu_count=0
    local check_times=6  # 30秒内检测6次，每次间隔5秒
    
    for i in $(seq 1 $check_times); do
        # 获取进程CPU占用率（保留小数）
        CPU_USAGE=$(top -p "$PID" -b -n 1 | tail -1 | awk '{print $9}')
        
        # 检查CPU_USAGE是否为数字（包含小数）
        if [[ "$CPU_USAGE" =~ ^[0-9]+\.?[0-9]*$ ]]; then
            # 使用bc进行浮点数比较
            if [ "$(echo "$CPU_USAGE > $ACTUAL_THRESHOLD" | bc)" -eq 1 ]; then
                high_cpu_count=$((high_cpu_count + 1))
            fi
        fi
        
        # 如果不是最后一次检测，等待5秒
        if [ $i -lt $check_times ]; then
            sleep 5
        fi
    done
    
    # 如果超过一半的检测都显示CPU占用过高，返回0（需要重启）
    if [ $high_cpu_count -gt $((check_times / 2)) ]; then
        return 0  # 需要重启
    else
        return 1  # 不需要重启
    fi
}

# 检测进程是否存在
check_process_exists() {
    PID=$(pgrep -f "$PGREP_PATTERN")
    if [ -n "$PID" ]; then
        return 0  # 进程存在
    else
        return 1  # 进程不存在
    fi
}

# 进程存在监控 - 重启后5分钟内每分钟检测一次
process_monitor() {
    local check_count=5  # 检测5次，每次间隔1分钟
    
    for i in $(seq 1 $check_count); do
        sleep "$PROCESS_CHECK_INTERVAL"
        
        if ! check_process_exists; then
            # 进程不存在，直接返回，不再继续CPU监控
            return 1
        fi
    done
    
    # 5分钟内进程一直存在，返回成功
    return 0
}

# CPU监控循环 - 只在重启后执行一次检测
cpu_monitor_once() {
    # 先监控5分钟内进程是否存在
    if ! process_monitor; then
        # 进程在5分钟内消失了，不进行CPU检测
        return
    fi
    
    # 进程存在，检测CPU占用
    if check_cpu_usage; then
        # CPU占用过高，重启进程
        kill_process
        # 重启后递归调用，继续监控
        cpu_monitor_once
    fi
    # 如果CPU正常，函数结束，不再监控
}

# --- 主循环 (无日志输出) ---
while true; do
    NOW_TS=$(date +%s)
    TARGET_TODAY_TS=$(date -d "$TARGET_TIME" +%s)
    
    # 如果当前时间已经等于或超过了今天的目标时间
    if [ "$NOW_TS" -ge "$TARGET_TODAY_TS" ]; then
        # 则目标是明天的这个时间
        TARGET_TS=$(date -d "tomorrow $TARGET_TIME" +%s)
    else
        # 否则，目标就是今天的这个时间
        TARGET_TS=$TARGET_TODAY_TS
    fi
    
    # 计算需要休眠的秒数
    SLEEP_SECONDS=$((TARGET_TS - NOW_TS))
    
    # 执行休眠
    sleep "$SLEEP_SECONDS"
    
    # 从休眠中醒来，获取今天是周几
    # date +%u 的输出：1=周一, 2=周二, ..., 6=周六, 7=周日
    DAY_OF_WEEK=$(date +%u)
    
    # 判断是否为工作日 (周一到周五)
    # 如果 DAY_OF_WEEK 小于 6，则条件为真
    if [ "$DAY_OF_WEEK" -lt 6 ]; then
        # 是工作日，执行杀死进程的任务
        kill_process
        
        # 启动CPU监控（后台运行）
        cpu_monitor_once &
    fi
    
    # 强制休眠2秒，以确保安全度过当前的时间窗口（例如20:35），
    # 避免在下一次循环中因各种意外情况陷入"忙循环"。
    sleep 2
done
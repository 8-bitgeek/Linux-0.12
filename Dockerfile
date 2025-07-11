FROM ubuntu:16.04

# 设置非交互模式，避免 tzdata 等安装时卡住
ENV DEBIAN_FRONTEND=noninteractive

# 安装编译工具、vim、locale、中文字体等
RUN apt update && apt install -y \
    gcc \
    make \
    vim \
    git \
    locales \
    binutils \
    bin86 \
    fonts-noto-cjk \
 && rm -rf /var/lib/apt/lists/*

# 生成 en_US.UTF-8 locale
RUN locale-gen en_US.UTF-8

# 设置 UTF-8 环境变量（英文界面 + 支持中文）
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

# 设置默认工作目录
WORKDIR /root/Linux-0.12

# 可选：复制你的 Linux 0.12 源码进来（如果有）
# COPY /root/workspace/c/Linux-0.12 /root/Linux-0.12

# 可选：设置默认启动命令
CMD ["/bin/bash"]


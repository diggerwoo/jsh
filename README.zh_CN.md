# JSH - A Simple Jailed Shell
中文 | [English](README.md)

JSH 一个简单的 Jailed Shell 工具，部署 JSH 并不需要复杂的 docker 或 chroot 配置，只需要一个可执行 shell 文件，以及一个或若干简单的配置文件。

如果应用场景如下，那么 JSH 可能是适合你的：
- 需要限定某一组或某个用户，只能访问有限的 Linux 命令，比如只能 crontab -e，只能 ssh 到限定范围的主机
- 这些用户不是高级 Linux 管理员，不需要管道过滤、grep 这些相对复杂的操作

部署 JSH 需要的步骤：
1. 编译和安装 jsh ，注意 jsh 依赖 [libocli](https://github.com/diggerwoo/libocli)，需要先编译安装 libocli
2. 然后编辑用户组或用户的命令配置文件
3. 编辑 /etc/passwd，或者 usermod -s .. -g .. 改变用户的 shell 和 group

然后用户登录后就进入到一个限定命令范围的 shell 环境了，所能访问的命令就是上面第二步配置文件里所指定的。

## 编译安装
```sh
make
make install
```
上述步骤完成后，可执行文件 jsh 被安装到 /usr/local/bin，一个配置例子文件 group.jailed.conf 被安装到 /usr/local/etc/jsh.d 目录。

## 编辑组或用户配置文件

假设用户属于组 "jailed" ，那么用户对应的组配置文件就是 /usr/local/etc/jsh.d/group.jailed.conf。
jsh 配置设计为以组文件优先，即先尝试加载组配置文件，再尝试加载用户配置文件，这样做的目的是尽量减少配置。
如果对于组内的个别用户需要额外的命令，比如 admin 用户，那么再去配置 /usr/local/etc/jsh.d/user.admin.conf，增加 admin 所需要的其它命令。
配置文件说明见最后一节。
在 [group.jailed.conf](conf/group.jailed.conf) 这个例子里，我们允许 jailed 组用户：
- 执行 id, pwd, ls, vim, ping, ssh, crontab -e 这几个命令.
- 使用 scp 访问自己 HOME 目录，以及 /home/public 这个公共目录，做上传下载。

```sh
# 允许 scp 传文件，除了 HOME，允许 scp 访问 /home/public
env SCPEXEC=1
env SCPDIR=/home/public

# ls 别名
alias ls "ls -a"

# 允许的命令语法
id
pwd
ls -l [ PATH ]
ls [ PATH ]
mkdir PATH
rm [ -rf ] PATH
vim [ PATH ]
ping [ -c INT ] { IP_ADDR | DOMAIN_NAME }
ssh NET_UID
crontab -e
```

## 修改用户的组和 shell 属性

以 jailuser 为例，将用户改变到 jailed 组，指定 login shell 为 jsh：
> 注意使用 usermod 前，需要将 /usr/local/bin/jsh 加入到 /etc/shells
```
usermod -g jailed -s /usr/local/bin/jsh jailuser
```

之后用户 jailuser ssh 登录后就进入到 jsh，jsh 使用风格是 Cisco-like 的，敲 ? 就提示了可访问的命令或词法帮助，TAB 关键字补齐，man 可以查看简单语法：

```
[centos71 jailuser]>
  cat                    - Concatenate files and print on the standard output
  cd                     - Change directory
  crontab                - Maintains crontab files for individual users
  exit                   - Exit jsh
  id                     - Print real and effective user and group IDs
  ls                     - List directory contents
  man                    - Display manual text
  pwd                    - Print name of current/working directory
  ssh                    - OpenSSH SSH client (remote login program)
  vim                    - Vi IMproved, a programmers text editor
[centos71 jailuser]>
[centos71 jailuser]> man ssh
NAME
        ssh - - OpenSSH SSH client (remote login program)
SYNOPSIS
        ssh NET_UID
[centos71 jailuser]> man crontab
NAME
        crontab - Maintains crontab files for individual users
SYNOPSIS
        crontab -e
```

## 配置文件说明

//TODO

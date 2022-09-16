# JSH - A Simple Jailed Shell
中文 | [English](README.md)

JSH 一个简单的 Jailed Shell 工具，部署 JSH 并不需要复杂的 docker 或 chroot 配置，只需要一个可执行 shell 文件，以及一个或若干简单的配置文件。

如果应用场景如下，那么 JSH 可能是适合你的：
- 需要限定某一组或某个用户，只能访问有限的 Linux 命令，比如只能 crontab -e，只能 ssh 到限定范围的主机
- 这些用户不是高级 Linux 管理员，不需要管道过滤、grep 这些相对复杂的操作

部署 JSH 需要的步骤：
1. 编译和安装 jsh ，注意 jsh 依赖 [libocli](../libocli)，需要先编译安装 libocli
2. 然后编辑用户组或用户的命令配置文件
3. 编辑 /etc/passwd，或者 usermod -s .. -g .. 改变用户的 shell 和 group

然后用户登录后就进入到一个限定命令范围的 shell 环境了，所能访问的命令就是上面第二步配置文件里所指定的。

## 编译安装
```sh
make
make install
```

## 编辑组或用户配置文件

以组 example 为例，编辑 /usr/local/etc/jsh.d/group.example.conf，
在这个例子里，我们允许 example 组的用户执行 id, pwd, ls, vim, ssh, crontab -e 这几个命令，
并且允许用户使用 scp 访问自己 HOME 目录，或 /home/public 这个公共目录，做上传下载操作。
```sh
# 允许 scp 传文件，除了 HOME，允许 scp 访问 /home/public
env SCPEXEC=1
env SCPDIR=/home/public

# ls 别名
alias ls "ls -a"

id
pwd
ls -l [ PATH ]
ls [ PATH ]

# 允许vi
vim [ PATH ]

# 允许 ssh 
ssh NET_UID

# 允许修改自己的 crontab
crontab -e
```

## 修改用户的组和 shell 属性

以 jailuser 为例，将用户改变到 example 组，指定 login shell 为 jsh：
> 注意使用 usermod 前，需要将 /usr/local/bin/jsh 加入到 /etc/shells
```
usermod -g example -s /usr/local/bin/jsh jailuser
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

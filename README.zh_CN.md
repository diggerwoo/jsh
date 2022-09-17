# JSH - A Simple Jailed Shell
中文 | [English](README.md)

JSH 一个简单的 Jailed Shell 工具，部署 JSH 并不需要复杂的 docker 或 chroot 配置，只需要一个可执行 shell 文件，以及一个或若干简单的配置文件。

如果应用场景如下，那么 JSH 可能是适合你的：
- 需要限定某一组或某个用户，只能访问有限的 Linux 命令，甚至命令选项也是限定的，比如只能 crontab -e，只能 ssh 到某几台主机
- 这些用户不是高级 Linux 用户，不需要管道过滤、grep 这些相对复杂的操作

部署 JSH 需要的步骤：
1. 编译和安装 jsh ，注意 jsh 依赖 [libocli](https://github.com/diggerwoo/libocli)，需要先编译安装 libocli
2. 然后编辑用户组或用户的命令配置文件
3. 编辑 /etc/passwd，或者 usermod -s .. -g .. 改变用户的 shell 和 group

然后用户登录后就进入到一个限定命令范围的 shell 环境了，所能访问的命令就是上面第二步配置文件里所指定的。

## 1. 编译安装
```sh
make
make install
```
上述步骤完成后，可执行文件 jsh 被安装到 /usr/local/bin，一个配置例子文件 group.jailed.conf 被安装到 /usr/local/etc/jsh.d 目录。

## 2. 编辑组或用户配置文件

假设用户属于组 "jailed" ，那么用户对应的组配置文件就是 /usr/local/etc/jsh.d/group.jailed.conf。
jsh 配置设计为以组文件优先，即先尝试加载组配置文件，再尝试加载用户配置文件，这样做的目的是尽量减少配置。
如果对于组内的个别用户需要额外的命令，比如 admin 用户，那么再去配置 /usr/local/etc/jsh.d/user.admin.conf，增加 admin 所需要的其它命令。
配置文件说明详见 [第 4 节](#4-配置文件说明)。

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

## 3. 修改用户的组和 shell 属性

以 jailuser 为例，将用户改变到 jailed 组，指定 login shell 为 jsh：
> 注意使用 usermod 前，需要将 /usr/local/bin/jsh 加入到 /etc/shells
```
usermod -g jailed -s /usr/local/bin/jsh jailuser
```

之后 jailuser 用户 ssh 或 console 登录后就进入到 jsh 效果如下，jsh 的使用是类似 Cisco 风格的，敲 ? 提示可访问的命令或词法帮助，敲 TAB 自动补齐关键字或 PATH，内置的 man 命令可以查看简单语法：

 ![image](https://github.com/diggerwoo/blobs/blob/main/img/jsh.gif)

## 4. 配置文件说明

### 4.1 环境变量

定义环境变量，注意等号左右不能有空格，NAME 和 VALUE 内不能有空格或单双引号：
```
env <NAME>=<VALUE>
```
比如：  
```
env LANG=en_US.UTF-8
```

不需要再定义 PATH 变量，jsh 启动时会已经设置了 PATH=/bin:/sbin/:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin ，可确保查找到大多数 Linux 常用命令。

jsh 自定义的两个与 scp 相关的两个环境变量：  
 - SCPEXEC 设置为 1 或 TRUE 时，允许用户使用 scp 传文件
 - SCPDIR  设置用户除了 HOME 目录之外可访问的目录，用冒号可隔离多个目录
比如：  
```
env SCPEXEC=1
env SCPDIR=/home/public:/var/www
```

### 4.2 命令的别名

使用 alias 关键字定义命令别名，注意展开命令中有空格时，使用双引号包含：  
```
alias <首关键字> ”<展开的命令>"
```
比如：
```
alias ls "ls -a"
```
jsh 内部已经将 vi 和 vim 设置别名 "vim -Z"，即用户使用 vim 时不允许执行外部命令。

### 4.3 增加可执行的命令语法

注意事项：
- 命令语法的首关键字必须是一个存在的可执行文件，比如 "history" 是 bash 内部命令关键字，系统中并没有 history 这个可执行文件，那么增加 history 语法是无效的。
- "cd", "exit" 是 jsh 自带的命令关键字，不需要重复添加。
- 使用小写单词作为命令关键字（Linux 的命令都是小写的），大写单词通常用于表达词法关键字，如果大写单词不匹配任何词法名字，则被认为是关键字。jsh 定义的词法关键字见 [4.4节](#44-保留的词法关键字)。
- 在组配置文件中出现过的命令语法，不需要在用户配置文件中重复添加，即用户文件中只配置用户额外需要的命令语法即可。
- 语法内规定的选项、参数格式以及顺序，都需要跟外部命令相匹配，否则执行时会出错。

例如，允许用户 mkdir 创建目录，允许修改自己的 crontab：
```
mkdir PATH
crontab -e 
```

命令语法中可使用语法保留字符 **[ ] { | }** ：
- **多选一** 语法段 **{ | }**  ，每个符号单词之间必须使用 **|** 分隔，比如 " { add | del } "
- **可选项** 语法段 **[  ]**，一个段落内可包含多个符号单词，比如 " [ -c COUNT ] [ -s SIZE ] "

语法保留字符的限制：
- 特殊字符之间，以及特殊字符与其他符号单词之间，必须用空格隔离
- 多选一段落 **{ }** 之内不允许再嵌套任何 **[ ]** 或 **{ }**
- 可选项段落 **[ ]** 内允许嵌套一层多选一 **{ }**，比如 " [ -c { 5 | 10 | 100 } ] "

例如，允许用户使用 ping IP 地址或域名，允许用户使用 -c 选项选择报文发送次数为 5、10 或 100个
```
ping [ -c { 5 | 10 | 100 } ] { IP_ADDR | DOMAIN_NAME }
```

例如，只允许用户 ssh 使用 admin 身份登录 192.168.1.1 ，以及使用 guest 身份登录 192.168.1.3 ：
```
ssh { admin@192.168.1.1 | guest@192.168.1.3 }
```

### 4.4 保留的词法关键字

常用的词法关键字：
| 词法类型 | 说明 |
| :--- | :--- |
| PATH | 文件或目录路径，可 TAB 补齐 |
| WORDS | 字符串格式，可双引号包含内含空格，比如 "test 123" |
| WORD | 字母开始的单词 |
| UID | 含字母数字_.的用户名或用户ID |
| DOMAIN_NAME | 域名 |
| HOST_NAME | 主机名或域名 |
| IP_ADDR | IPv4 地址 |
| INT | 10进制整型 |
| HTTP_URL | HTTP URL |
| HTTPS_URL | HTTPS URL |

其它不常用的词法：
| 词法类型 | 说明 |
| :--- | :--- |
| HEX | 16进制整型 |
| IP_MASK | IPv4 掩码 |
| IP_PREFIX | IPv4Addr/<0-32> 即前缀式子网 |
| IP_BLOCK | IPv4Addr[/<0-32>] |
| IP_RANGE | IPAddr1[-IPAddr2] IP地址范围 |
| IP6_ADDR | IPv6Addr |
| IP6_PREFIX | IPv6Addr/<0-128> 前缀式子网 |
| IP6_BLOCK | IPv6Addr[/<0-128>] |
| PORT | <0-65535> TCP/UDP Port |
| PORT_RANGE | Port1[-Port2] |
| VLAN_ID | <1-4094> VLAN ID |
| MAC_ADDR | MAC 地址 |
| EMAIL_ADDR | EMAIL 地址 |


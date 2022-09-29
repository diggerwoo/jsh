# JSH - 简单易用的 Jailed Shell 工具
中文 | [English](README.md)

JSH 是一个适用于 Linux 平台的 Jailed Shell 工具，部署 JSH 并不需要复杂的 docker 或 chroot 配置，只需要一个可执行 jsh 文件，以及一个或若干简单的配置文件。

如果应用场景如下，那么 JSH 可能是适合你的：
- 需要限定某一组或某个用户，只能访问有限的 Linux 命令，甚至命令选项也是限定的，比如：只能 ssh 到某几台指定的主机。
- 受限用户 ssh 或 sftp 登录后，以及 scp 时，都只能访问自己的 HOME 目录，以及指定的公共目录。
  > SFTP 的 HOME Jail 是基于 ptrace 系统调用实现的，目前只在 X86_64 平台测试通过。
- 主机环境不需要受限用户做相对复杂的 shell 语法操作，但可能需要支持基本的管道过滤、重定向操作。

部署 JSH 需要的步骤：
1. 编译和安装 jsh ，注意 jsh 依赖 [libocli](https://github.com/diggerwoo/libocli)，需要先编译安装 libocli （目前版本要求更新至 libocli 0.91）。
2. 编辑用户组或用户的命令配置文件。
3. 编辑 /etc/passwd，或者 usermod -s /usr/local/bin/jsh -g <受限用户组> 改变用户的 shell 和 group 属性。

之后用户再登录就会进入一个受限的 shell 环境，所能访问的命令就是上面第 2 步配置文件里所指定的。

## 1. 编译安装
```sh
make
make install
```
上述步骤完成后，可执行文件 jsh 被安装到 /usr/local/bin，一个配置例子文件 group.jailed.conf 被安装到 /usr/local/etc/jsh.d 目录。


## 2. 编辑组或用户配置文件

jsh 的配置文件部署于 /usr/local/etc/jsh.d 下。
假设用户 "testuser" 属于组 "jailed" ，那么用户对应的组配置文件就是 "group.jailed.conf"，用户配置文件为 "user.testuser.conf" 。
jsh 配置设计为以组文件优先，即先尝试加载组配置文件，再尝试加载用户配置文件。
若一个组内所有用户控制策略相同，那么本组只需要配置一个组文件，而不需要配置任何用户文件。
若组内有个别用户需要额外的命令，比如 admin 用户，那么再去配置 user.admin.conf，增加 admin 所需要的其它命令。
配置文件说明详见 [第 4 节](#4-配置文件说明)。

在 [group.jailed.conf](conf/group.jailed.conf) 这个例子里，我们允许 jailed 组用户：
- 执行 id, pwd, passwd, ls, vim, mkdir, rm, ping, ssh, grep 这几个命令，其中允许 grep 输出分页或重定向。
- 使用 sftp 和 scp 访问自己 HOME 目录和 /home/public 这个公共目录进行文件上传下载。

```sh
# 允许 sftp 和 scp 传文件，除了 HOME，允许访问 /home/public
env SCPEXEC=1
env SCPDIR=/home/public

# ls 别名，增加隐藏文件输出选项，和彩色输出选项
alias ls "ls -a --color=auto"
# grep 别名，增加彩色输出选项
alias grep "grep --color=auto"

# 允许的命令语法
id
pwd
passwd
ls -l [ PATH ]
ls [ PATH ]
mkdir PATH
rm [ -rf ] PATH
vim [ PATH ]
ping [ -c INT ] { IP_ADDR | DOMAIN_NAME }
ssh NET_UID

# 允许 grep 结果分页输出，或重定向到文件
grep [ -v ] WORDS PATH
grep [ -v ] WORDS PATH | more
grep [ -v ] WORDS PATH > PATH
```

## 3. 修改用户的组和 shell 属性

以 jailuser 为例，将用户改变到 jailed 组，指定 login shell 为 jsh：
> 注意使用 usermod 前，需要将 /usr/local/bin/jsh 加入到 /etc/shells 。
```
usermod -g jailed -s /usr/local/bin/jsh jailuser
```

之后 jailuser 用户 ssh 或 console 登录后就进入到 jsh ，其执行效果如下。jsh 的使用类似 Cisco 风格，敲 ? 提示可访问的命令或词法帮助，敲 TAB 自动补齐关键字或 PATH，jsh 内置的 man 命令可以查看简单语法：

 ![image](https://github.com/diggerwoo/blobs/blob/main/img/jsh.gif)


## 4. 配置文件说明

### 4.1 环境变量

使用 env 关键字定义环境变量，注意等号左右不能有空格，NAME 和 VALUE 内不能有空格或单双引号：
```
env <NAME>=<VALUE>
```
比如：  
```
env LANG=en_US.UTF-8
```

配置文件中不需要定义 PATH 环境变量，jsh 启动时会自动设置 PATH=/bin:/sbin/:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin ，可确保查找到大多数 Linux 常用命令。

如果同一个环境变量名被重复多次定义，那么最后一个有效。在用户文件中定义的环境变量优先级高于组文件中的同名环境变量。

jsh 的内部环境变量如下：  
 - SCPEXEC 设置为 1 或 TRUE 时，允许用户使用 sftp 后 scp 传文件。
 - SCPDIR  设置用户除 HOME 目录之外可访问的目录，用冒号可隔离多个目录。
 - SCP_SERVER 设置 sftp-server 路径，默认为 /usr/libexec/openssh/sftp-server，若所在系统 sftp-server 的路径与此不一致，则需要手工配置此环境变量，路径与 ssh_config 文件中的 subsystem sftp 指定的 sftp-server 路径相同。
 > 注意 sshd_config 文件中的 subsystem sftp 不能配置为 internal-sftp，internal-sftp 无法与 jsh 配合实现 HOME 目录访问限制。
 - HOMEJAIL 设置用户 SSH 或 CONSOLE 登录后是否被限制只能访问 HOME 目录和公共目录，默认启用，设置为 0 或 False 时禁用。
 > 启用 HOMEJAIL 时，用户 cd 和 vim 被限制在 HOME 和公共目录内。但对于其它命令，HOMEJAIL 实际上通过限制 PATH 参数来实现的，即用户输入的 PATH 参数不能超出上述目录之外。如果命令参数词法配置错误，比如把 "ls PATH" 设置为 "ls WORDS"，那么会使得 "ls" 命令的 HOMEJAIL 失效。详见[4.3节](#43-增加可执行的命令语法)。

比如：  
```
env SCPEXEC=1
env SCPDIR=/home/public:/var/www
env SFTP_SERVER=/usr/libexec/openssh/sftp-server
```


### 4.2 命令的别名

使用 alias 关键字定义命令别名，注意若展开命令中有空格时，应使用双引号包含整个展开命令串：  
```
alias <首关键字> ”<展开的命令>"
```

与环境变量类似，同关键字 alias 重复定义时，最后一个有效。

比如：
```
alias ls "ls -a"
```
jsh 内部已经将 vi 和 vim 设置别名 "vim -Z"，即不允许用户在 vim 时使用 :! 执行外部命令。

### 4.3 增加可执行的命令语法

注意事项：
- 命令语法的首关键字必须对应一个可执行的外部命令文件（除非使用 alias 另行指向可执行文件），比如 "logout" 是 bash 内部命令，但系统中并没有 logout 这个可执行文件，那么在配置文件中增加 logout 语法就是无效的。
- "cd", "exit", "history" 是 jsh 自带的命令关键字，不需要重复添加。
- 使用小写单词作为命令关键字（Linux 的命令都是小写的），大写单词通常用于表达词法类型，如果大写单词不匹配任何词法类型，则被认为是命令关键字。最常用的词法类型为 "PATH"，即目录或文件路径，当指定词法 PATH 时，键入 TAB 时 jsh 会自动对路径补齐，键入双 TAB 时则会罗列所有可能的路径匹配，类似 bash 的行为。jsh 定义的详细词法类型见 [4.4节](#44-jsh-的词法类型)。
- 在组配置文件中出现过的命令语法，不需要在用户配置文件中重复添加，即在用户文件中只配置用户额外需要的命令语法即可。
- 语法内规定的选项、参数格式以及顺序，都需要跟外部命令的选项、参数规范相匹配，否则 jsh 调用外部命令时会出错。

例如，允许用户 mkdir 创建目录，rmdir 删除目录：
```
mkdir PATH
rmdir PATH
```

命令语法中可使用 jsh 的语法保留特殊字符 **[ ] { | }** ：
- **多选一** 语法段 **{ | }**  ，每个符号单词之间必须使用 **|** 分隔，比如 " { add | del } "
- **可选项** 语法段 **[  ]**，一个段落内可包含多个符号单词，比如 " [ -c INT ] [ -s INT ] "

语法保留特殊字符的限制：
- 特殊字符之间，以及特殊字符与其他符号单词之间，必须用空格隔离
- 多选一段落 **{ }** 之内不允许再嵌套任何 **[ ]** 或 **{ }**
- 可选项段落 **[ ]** 内允许嵌套一层多选一 **{ }**，比如 " [ -c { 5 | 10 | 100 } ] "

例一，允许用户使用 ping IP 地址或域名，允许用户使用 -c 选项选择报文发送次数为 5、10 或 100个：
```
ping [ -c { 5 | 10 | 100 } ] { IP_ADDR | DOMAIN_NAME }
```

例二，只允许用户 ssh 使用 admin 身份登录 192.168.1.1 ，或使用 guest 身份登录 192.168.1.3 ：
```
ssh { admin@192.168.1.1 | guest@192.168.1.3 }
```

### 4.4 jsh 的词法类型

常用的词法关键字：
| 词法类型 | 说明 |
| :--- | :--- |
| PATH | 文件或目录路径，可 TAB 补齐 |
| WORDS | 字符串格式，可双引号包含内含空格，比如 "test 123" |
| WORD | 字母开始的单词 |
| UID | 含字母数字_.的用户名或用户ID |
| NET_UID | UID@域名 或 UID@IPv4地址 |
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

### 4.5 改进命令选项提示信息

当键入 ? 时，jsh 会提示命令用法，但对于命令的选项，比如 ls 的 -l 选项，提示是空的。
```
[centos71 jailuser]> ls
  -l                     -
  Path name              - Path name
  <Enter>                - End of command
```

如果需要更友好的提示，那么需要额外去配置 /usr/local/etc/jsh.d/man.conf，这个配置文件很简单，关键字单独一行，之后跟随一个或多个缩进的选项行（空格或TAB缩进都可以），参见范例 [man.conf](conf/man.conf)。
比如在 man.conf 中配置 ls 命令的 -l 选项提示：
```
ls
  -l            "use a long listing format"
```

保存配置后重登录，再 ls 后键入 '?' ，会得到如下提示，显然友好很多：

```
[centos71 jailuser]> ls
  -l                     - Use a long listing format
  Path name              - Path name
  <Enter>                - End of command
```


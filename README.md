# JSH - A Simple Jailed Shell
English | [中文](README.zh_CN.md)

JSH is a simple Jailed Shell tool. 
The Deployment of JSH is easy and does not require complicated docker or chroot, only an executable jsh and simple configuration files will be needed.

If the application scenario is as follows, then JSH may be suitable for you:
- Need to limit certain groups or certain users to only access limited Linux commands. E.g. crontab -e, ssh to a limited range of hosts.
- These users are not advanced Linux administrators and do not need operations such as pipe filtering, grep.

Key steps required to deploy JSH:
1. Compile and install jsh, note that jsh depends on [libocli](https://github.com/diggerwoo/libocli), you need to compile and install libocli first.
2. Edit the configuration file for specific group or user, to include all the limited commands.
3. Edit /etc/passwd, or usermod -s .. -g .. to change the user's shell and group.

Then after logging in the user will enter a jailed shell environment, only those commands listed in the configuration file can be accessed.


## 1. Build and install
```sh
make
make install
```
After making process, the jsh will be installed into /usr/local/bin, and a sample group.jailed.conf will be installed into /usr/local/etc/jsh.d directory. 

## 2. Edit group or user configuration

For example a user belongs the group "jailed", then the coresponding configuration file of the group "jailed" will be "/usr/local/etc/jsh.d/group.jailed.conf".
The jsh will try to load the group configuration first, and then try to load the user specific configuration. The purpose of this is to minimize the configurations. If additional commands are needed for individual users in the group, such as the “admin”, then go to configure /usr/local/etc/jsh.d/user.admin.conf and add other commands. See the [section 4](#4-Configuration-file) for detailed description of configuration file.

In the sample configuration [group.jailed.conf](conf/group.jailed.conf), we allow users of jailed group to:
- excecute limited comands: id, pwd, ls, mkdir, rm. vim, ping, ssh, crontab -e .
- use scp to upload/download file in self HOME and extra /home/public directory.

```sh
# Allow acp, and acces extra /home/public besides HOME.
env SCPEXEC=1
env SCPDIR=/home/public

# Alias settings
alias ls "ls -a"

# Allowed commands
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

## 3. Change user shell and group

For example, change the group of user "jailuser" to "jailed"，and change login shell to "/usr/local/bin/jsh":
> If you use usermod to change login shell，you should add /usr/local/bin/jsh into /etc/shells first.
```
usermod -g jailed -s /usr/local/bin/jsh jailuser
```
After that, when jailuser ssh logs in the shell environment will like blow. 
The usage of jsh is Cisco-like, typing ? will get command or lexical help, typing TAB will get keyword or path auto completed.
The man command can be used to display brief command syntaxes.

 ![image](https://github.com/diggerwoo/blobs/blob/main/img/jsh.gif)

## 4. Configuration file

### 4.1 Environment variables

The environment definition is quite like Linux's. Note there can be no spaces beside the ‘=’, and no spaces or single/double quotation marks inside <NAME> and <VALUE>:
```
env <NAME>=<VALUE>
```
For example:
```
env LANG=en_US.UTF-8
```

There is no need to define the PATH. When jsh starts it sets "PATH=/bin:/sbin/:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin" to make sure that it can  find most of common commands of Linux.

There are two internal env vars defined by jsh：  
 - SCPEXEC, set 1 or TRUE if users/groups are allowed to use scp.
 - SCPDIR, defines accessible directories other than HOME. Multi directory shoud be separated by ':'.

For example:
```
env SCPEXEC=1
env SCPDIR=/home/public:/var/www
```

### 4.2 Alias

The keyword "alias" is used to define alias of commands. Note that double quotation marks must be used if there are SPACEs inside the expanded command.
```
alias <keyword> ”<expanded command>"
```
For example:
```
alias ls "ls -a"
```
The “vi” and “vim” has been aliased as "vim -Z" by jsh, to avoid user executing external Linux commands inside vim.

### 4.3 Add permitted command syntaxes

Configure each permitted command as a syntax line in the configuration file. Precautions:
- The first keyword of each command syntax coresponds to a existing excetuable file. For example, "history" is a bash internal command keyword, there is no executable "history" present in any bin or sbin directory, so adding a "history" line is invalid.
- "cd", "exit" are jsh builtin commands and do not need to be added repeatedly.
- Use lowercase words as command keywords (Linux commands are all lowercase). Uppercase words are usually used to express lexical keywords. If the uppercase word does not match any lexical types, it is considered a keyword. Refer to [Section 4.4](#44-lexical-types-of-jsh) for more details about jsh lexical types.
- The command syntaxes in the group configuration file do not need to be added repeatedly in the user configuration file. That is, only the extra command syntax required by the user should be configured in the user file.
- The parameter formats and sequences specified in the syntax need to match the requirements of the actual commands, otherwise error will occur during execution.

Below example allows user to make new directory and update self cron jobs.
```
mkdir PATH
crontab -e 
```

Special syntax charactors **[ ] { | }** are allowed for option or alternatives in the syntax line.
- **Alternative** segment **{ | }**  , allows two of more tokens separated by '**|**' . E.g. " { block | pass } "，" { tcp | udp | icmp } "
- **Optional** segment **[  ]**, each segment can have multiple tokens. E.g. " [ -c COUNT ] [ -s PKT_SIZE ] "

Usage limitaions of syntax charactors:
- SPACE must be present between reserved chars, or between reserved char and other tokens.
- NO **[ ]** or **{ }** are allowed to be nested inside **{ }**.
- **{ }** can be nested inside **[ ]**. E.g.  " [ from { IP_ADDR | IFNAME } ] "

Below example allows user to ping IP or domain name, and to use -c option to choose the number of ICMP packets amonng 5, 10, and 100.
```
ping [ -c { 5 | 10 | 100 } ] { IP_ADDR | DOMAIN_NAME }
```

Another example permits user to ssh only two sites, including to 192.168.1.1 as admin, and to 192.168.1.3 as guest.
```
ssh { admin@192.168.1.1 | guest@192.168.1.3 }
```

### 4.4 Lexical types of jsh

Commonly used lexical keywords are as follows.
| Lexical Keyword | Description |
| :--- | :--- |
| PATH | PATH of directory or file, supports TAB auto completion |
| WORDS | Any string, supports SPACEs in double quotation, like "test 123" |
| WORD | Word starts with alphabet |
| UID | User ID composed by alphabets, digits, '_' and '.' |
| DOMAIN_NAME | Domain name |
| HOST_NAME | Hostname or domain name|
| IP_ADDR | IPv4 address |
| INT | Integer |
| HTTP_URL | HTTP URL |
| HTTPS_URL | HTTPS URL |


Commonly used lexical types are as follows.
| Lexical Keyword | Description |
| :--- | :--- |
| HEX | Hexidecimal |
| IP_MASK | IPv4 mask |
| IP_PREFIX | IPv4Addr/<0-32> |
| IP_BLOCK | IPv4Addr[/<0-32>] |
| IP_RANGE | IPAddr1[-IPAddr2] |
| IP6_ADDR | IPv6Addr |
| IP6_PREFIX | IPv6Addr/<0-128> |
| IP6_BLOCK | IPv6Addr[/<0-128>] |
| PORT | <0-65535> TCP/UDP Port |
| PORT_RANGE | Port1[-Port2] |
| VLAN_ID | <1-4094> VLAN ID |
| MAC_ADDR | MAC address |
| EMAIL_ADDR | EMail address |


# JSH - An easy-to-use Jailed Shell
English | [中文](README.zh_CN.md)

JSH is an easy-to-use Jailed Shell tool for Linux. 
The Deployment of JSH is easy and does not require complicated docker or chroot, only an executable jsh and simple configuration files will be needed.

If the application scenario is as follows, then JSH may be suitable for you:
- Need to limit certain groups or certain users to only access limited Linux commands, and even command options are limited. E.g. ssh to limited range of hosts.
- Need to restrict user sftp or scp to only access their own HOME directory, as well as the specified public directory.
  > The HOME Jail of SFTP is implemented based on ptrace. It works on X86_64 but has not been tested on i386.

Key steps required to deploy JSH:
1. Compile and install jsh, note that jsh depends on [libocli](https://github.com/diggerwoo/libocli), you need to compile and install libocli first.  (Current jsh version needs libocli 0.92).
2. Edit the configuration file for specific group or user, to include all the limited commands.
3. Edit /etc/passwd, or usermod -s /usr/local/bin/jsh -g <limited_group> to change the user's shell and group.

Then after logging in the user will enter a jailed shell environment, only those commands listed in the configuration file can be accessed.


## 1. Build and install
```sh
make
make install
```
After making process, the jsh will be installed into /usr/local/bin, and a sample group.jailed.conf will be installed into /usr/local/etc/jsh.d directory. 

## 2. Edit group or user configuration

The jsh configuration is deployed under “/usr/local/etc/jsh.d”. Assuming user "testuser" belongs to group "jailed", then the group configuration file will be “group.jailed.conf” , while the user file will be “user.testuser.conf” .
The jsh will try to load the group configuration first, and then try to load the user specific configuration. 
If all users in a group have the same control policy, only one group file needs to be configured.
If additional commands are needed for specific users in the group, such for user “admin”, then go to configure “user.admin.conf” . See the [section 4](#4-GroupUser-Configuration-file) for detailed description of configuration file.

In the sample configuration [group.jailed.conf](conf/group.jailed.conf), we allow users of jailed group to:
- execute limited commands: id, pwd, passwd, ls, mkdir, rm, vim, ping, ssh .
- use scp to upload/download file in self HOME and extra /home/public directory.

```sh
# Allow sftp or scp to access HOME directory, as well as /home/public.
env SCPEXEC=1
env SCPDIR=/home/public

# Alias settings for ls and grep, with colored output enabled
alias ls "ls -a --color=auto"
alias grep "grep --color=auto"

# Allowed commands
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

# Allow grep output to be paged or redirected.
grep [ -v ] WORDS PATH
grep [ -v ] WORDS PATH | more
grep [ -v ] WORDS PATH > PATH
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

## 4. Group/User Configuration file

### 4.1 Environment variables

The keyword "env" is used to define environment. Note there can be no SPACEs beside the ‘=’, and no SPACEs or single/double quotation marks inside NAME and VALUE:
```
env <NAME>=<VALUE>
```
For example:
```
env LANG=en_US.UTF-8
```

There is no need to define the env PATH. When jsh starts it sets "PATH=/bin:/sbin/:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin" to make sure that it can find most of common commands of Linux.

If an env is defined multiple times, the latest one will take precedence. So if an env of group configuration is redefined in specific user configuration, the one in the user configuration will take precedence when this user logs in.

There are several internal env vars of jsh:  
 - SCPEXEC, set 1 or TRUE if users/groups are allowed to use sftp and scp.
 - SCPDIR, defines public directories other than user's home directory. Multi directory should be separated by ':'.
 - SCP_SERVER, sets the sftp-server path, default is /usr/libexec/openssh/sftp-server. You need to manually configure this if the path is different from your system's. The path should be the same as that specified by ”subsystem sftp“ in the opessh's ssh_config configuration file.
 > Note the "subsystem sftp" in sshd_config cannot be configured as ”internal-sftp“. The internal-sftp cannot work with jsh to implement the HOME directory jail.
 - HOMEJAIL, sets whether the user is restricted to access only the HOME/public directory after logging in with SSH or CONSOLE, enabled by default, disabled when set to 0 or False.
 > When HOMEJAIL is enabled, cd and vim can be restricted inside HOME/public directories. But for other commands, HOMEJAIL is actually achieved by restricting the PATH parameter. That is, the PATH parameter entered by the user cannot reach outside HOME/public directories. If the command parameter is misconfigured, e.g. configuring "ls WORDS" instead of "ls PATH", then the HOMEJAIL of the "ls" will fail. Refer to [section 4.3](#43-add-permitted-command-syntaxes) for more details.

For example:
```
env SCPEXEC=1
env SCPDIR=/home/public:/var/www
env SFTP_SERVER=/usr/libexec/openssh/sftp-server
```

### 4.2 Alias

The keyword "alias" is used to define alias of commands. Note that double quotation marks must be used if there are SPACEs inside the expanded command.
```
alias <keyword> ”<expanded command>"
```
Similar to the env definition, the last one takes precedence if same alias keyword is defined multiple times.

For example:
```
alias ls "ls -a"
```

The “vi” and “vim” has been internally aliased as "vim -Z" by jsh, to avoid user executing external Linux commands inside vim.

### 4.3 Add permitted command syntaxes

Configure each permitted command as a syntax line in the configuration file. Precautions:
- The first keyword of each command syntax must corespond to an existing executable file (except for alias). For example, "logout" is a bash internal command, but there is no executable "logout" present in any bin or sbin directory, so adding a "logout" syntax line is invalid.
- "cd", "exit" and "history" are jsh builtin commands and do not need to be added repeatedly.
- Use lowercase words as command keywords (Linux commands are all lowercase). Uppercase words are usually used for lexical types. If the uppercase word does not match any lexical types, it is considered a command keyword. The most commonly used lexical type is "PATH" which is used for directories or files. For “PATH” lexical type, jsh supports typing TAB key to auto-complete the path, and double-typing the TAB key to list all matching paths, which is similar to bash's behavior. Refer to [Section 4.4](#44-lexical-types-of-jsh) for more details about jsh lexical types.
- The command syntaxes in the group configuration do not need to be added repeatedly in the user configuration. That is, only those extra syntaxes  required by the user should be configured in the user file.
- The parameters‘ format and sequence specified in the syntax need to meet the requirements of the actual commands, otherwise error will occur during execution.

Below example allows user to make new directory and remove directory.
```
mkdir PATH
rmdir PATH 
```

Special syntax characters **[ ] { | }** are allowed for options or alternatives in the syntax line.
- **Alternative** segment **{ | }**  , allows two of more tokens separated by '**|**' . E.g. " { add | del } "
- **Optional** segment **[  ]**, each segment can have multiple tokens. E.g. " [ -c INT ] "

Usage limitations of syntax characters:
- SPACE must be present between reserved chars, or between reserved char and other tokens.
- NO **[ ]** or **{ }** are allowed to be nested inside **{ }**.
- **{ }** can be nested inside **[ ]**. E.g.  " [ -c { 5 | 10 | 100 } ] "

Below example allows user to ping IP or domain name, and to use -c option to choose the number of ICMP packets among 5, 10, and 100.
```
ping [ -c { 5 | 10 | 100 } ] { IP_ADDR | DOMAIN_NAME }
```

Another example permits user to ssh only two sites, including to 192.168.1.1 as admin, and to 192.168.1.3 as guest.
```
ssh { admin@192.168.1.1 | guest@192.168.1.3 }
```

### 4.4 Lexical types of jsh

Commonly used lexical types are as follows.
| Lexical Type | Description |
| :--- | :--- |
| PATH | PATH of directory or file, supports TAB auto completion |
| WORDS | Any string, supports SPACEs in double quotation, like "test 123" |
| WORD | Word starts with alphabet |
| UID | User ID composed by alphabets, digits, '_' and '.' |
| NET_UID | UID@DOMAIN_NAME or UID@IP_ADDR |
| DOMAIN_NAME | Domain name |
| HOST_NAME | Hostname or domain name|
| IP_ADDR | IPv4 address |
| INT | Integer |
| HTTP_URL | HTTP URL |
| HTTPS_URL | HTTPS URL |


Other infrequently used lexical types are as follows.
| Lexical Type | Description |
| :--- | :--- |
| HEX | Hexadecimal |
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

## 5. Other configurations and debug mode

### 5.1 man.conf

When typing ‘?’, jsh prompts for command usage. But for command options, such as the -l option of ls, the prompt is empty.
```
[centos71 jailuser]> ls
  -l                     -
  Path name              - Path name
  <Enter>                - End of command
```

If you need more friendly prompts, you can configure the /usr/local/etc/jsh.d/man.conf. The line format of man.conf is straightforward, with each line having two elements: keyword/option and manual text. The keyword line starts without any spaces and the manual text is optional, whilst the option line starts with space/TAB and manual text is mandatory. Please refer to [man.conf](conf/man.conf).
For example, configure the prompt of '-l' option of command 'ls':
```
ls
  -l            "use a long listing format"
```

After saving the configuration and log in again, type '?' after ‘ls ’ then you will get the following prompt which is obviously much more user-friendly:
```
[centos71 jailuser]> ls
  -l                     - Use a long listing format
  Path name              - Path name
  <Enter>                - End of command
```

### 5.2 banner.txt

The configuration file /usr/local/etc/jsh.d/banner.txt is used to edit the text displayed after users log in, such as displaying a welcome or help text.


### 5.3 Debug mode

The syslog facility of jsh is LOG_AUTHPRIV. On the CentOS platform, this type of log will go into the /var/log/secure by default.

JSH also provides a debug mode for syslog, which can trace the detailed jailing process. If a ".jsh_debug" file exists in the user directory, the debug  information will be recorded into syslog after the user logs in.

For example, to enable debug mode for the user "jailuser":
```
touch /home/jailuser/.jsh_debug
```

### 5.4 sftp.bypass

In case of sftp jailing with debug mode enabled, if you see unexpected "deny open()/openat()" debug logs for some paths and that really affect your sftp jailing, then you need to add the path into sftp.bypass configuration file. Carefully read the comments in /usr/local/etc/jsh.d/sftp.bypass .


### 5.5 Strictly limit SSH and SFTP service ports

If you need to limit different service ports for SSH and SFTP, for example, port 22 is only used for SSH to access jsh, and another high port 65522 is only used for SFTP or SCP, then you need to use /usr/local/etc/jsh.d/port.conf configuration file.

First edit /etc/ssh/sshd_config as below, and restart the sshd service:
```
Port 22
Port 65522
```

Then edit /usr/local/etc/jsh.d/port.conf as below:
```
strict_jsh_port 22
strict_sftp_port 65522
```

The port rules will take effect the next time you log in via SSH or SFTP.

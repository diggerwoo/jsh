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


## Build and install
```sh
make
make install
```
After making process, the jsh will be installed into /usr/local/bin, and a sample group.jailed.conf will be installed into /usr/local/etc/jsh.d directory. 

## Edit group or user configuration

For example a user belongs the group "jailed", then the coresponding configuration file of the group "jailed" will be "/usr/local/etc/jsh.d/group.jailed.conf".
The jsh will try to load the group configuration first, and then try to load the user specific configuration. The purpose of this is to minimize the configurations. If additional commands are needed for individual users in the group, such as the “admin”, then go to configure /usr/local/etc/jsh.d/user.admin.conf and add other commands. See the last section for configuration file descriptions.

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

## Change user shell and group

For example, change the group of user "jailuser" to "jailed"，and change login shell to "/usr/local/bin/jsh":
> If you use usermod to change login shell，you should add /usr/local/bin/jsh into /etc/shells first.
```
usermod -g jailed -s /usr/local/bin/jsh jailuser
```
After that, when jailuser ssh logs in and enters jsh environment. 
The jsh is Cisco-like, typing ? will get command or lexical help, typing TAB will get keyword or path auto completed.
The man command can be used to display brief command syntaxes.

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

## Configuration file

//TODO

#-
# Copyright (c) 2008-2009 Juan Romero Pardines.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#-

#
# Script to install packages into a sandbox in masterdir.
# Actually this needs the xbps-base-chroot package installed.
#

# Umount stuff if SIGINT or SIGQUIT was caught
trap umount_chroot_fs INT QUIT

prepare_chroot()
{
	local f=

	# Create some required files.
	touch $XBPS_MASTERDIR/etc/mtab
	for f in run/utmp log/btmp log/lastlog log/wtmp; do
		touch -f $XBPS_MASTERDIR/var/$f
	done
	for f in run/utmp log/lastlog; do
		chmod 644 $XBPS_MASTERDIR/var/$f
	done

	cat > $XBPS_MASTERDIR/etc/passwd <<_EOF
root:x:0:0:root:/root:/bin/bash
nobody:x:99:99:Unprivileged User:/dev/null:/bin/false
_EOF

	# Default group list as specified by LFS.
	cat > $XBPS_MASTERDIR/etc/group <<_EOF
root:x:0:
bin:x:1:
sys:x:2:
kmem:x:3:
tty:x:4:
tape:x:5:
daemon:x:6:
floppy:x:7:
disk:x:8:
lp:x:9:
uucp:x:10:
audio:x:11:
video:x:12:
utmp:x:13:
usb:x:14:
cdrom:x:15:
mail:x:34:
nogroup:x:99:
users:x:1000:
_EOF

	# Default file as in Ubuntu.
	cat > $XBPS_MASTERDIR/etc/hosts <<_EOF
127.0.0.1	xbps	localhost.localdomain	localhost
127.0.1.1	xbps

# The following lines are desirable for IPv6 capable hosts
::1     ip6-localhost ip6-loopback
fe00::0 ip6-localnet
ff00::0 ip6-mcastprefix
ff02::1 ip6-allnodes
ff02::2 ip6-allrouters
ff02::3 ip6-allhosts
_EOF

	# Use OpenDNS servers.
	cat > $XBPS_MASTERDIR/etc/resolv.conf <<_EOF
nameserver 208.67.222.222
nameserver 208.67.220.220
_EOF

	touch $XBPS_MASTERDIR/.xbps_perms_done

}

rebuild_ldso_cache()
{
	echo -n "==> Rebuilding chroot's dynamic linker cache..."
	chroot $XBPS_MASTERDIR /sbin/ldconfig -c /etc/ld.so.conf
	chroot $XBPS_MASTERDIR /sbin/ldconfig -C /etc/ld.so.cache
	echo " done."
}

install_xbps_utils()
{
	local needed fetch_cmd
	local xbps_prefix=$XBPS_MASTERDIR/usr/local

	for f in bin cmpver digest pkgdb; do
		if [ ! -x $xbps_prefix/sbin/xbps-${f} ]; then
			needed=yes
		fi
	done

	if [ -n "$needed" ]; then
		cd ${XBPS_MASTERDIR}/bin && ln -s dash sh
		echo "=> Installing the required XBPS utils."
		chroot $XBPS_MASTERDIR sh -c \
			"echo /usr/local/lib > /etc/ld.so.conf"
		fetch_cmd="$(which $XBPS_FETCH_CMD 2>/dev/null)"
		if [ -z "$fetch_cmd" ]; then
			echo "Unexistent XBPS_FETCH_CMD specified!"
			exit 1
		fi
		cp -f $fetch_cmd $xbps_prefix/sbin
		for f in bin cmpver digest pkgdb repo; do
			cp -f $XBPS_INSTALLDIR/sbin/xbps-$f.static \
				$xbps_prefix/sbin/xbps-$f
		done
		cp -f $XBPS_INSTALLDIR/sbin/xbps-src $xbps_prefix/sbin
		if [ -z $XBPS_INSTALLDIR ]; then
			installdir=/usr/share/xbps
		else
			installdir=$XBPS_INSTALLDIR/share/xbps
		fi
		cp -a $installdir $xbps_prefix/share
		rebuild_ldso_cache
	fi
}

xbps_chroot_handler()
{
	local action="$1"
	local pkg="$2"
	local only_destdir="$3"

	[ -z "$action" -o -z "$pkg" ] && return 1

	[ "$action" != "configure" -a "$action" != "build" -a \
	  "$action" != "install" -a "$action" != "chroot" ] && return 1

	mount_chroot_fs
	install_xbps_utils

	if [ ! -f $XBPS_MASTERDIR/.xbps_perms_done ]; then
		echo -n "==> Preparing chroot on $XBPS_MASTERDIR... "
		prepare_chroot
		echo "done."
	fi

	if [ "$action" = "chroot" ]; then
		env in_chroot=yes LANG=C chroot $XBPS_MASTERDIR /bin/bash
	else
		[ -n "$only_destdir" ] && \
			local lenv="install_destdir_target=yes"
		env in_chroot=yes LANG=C ${lenv} chroot $XBPS_MASTERDIR \
			xbps-src $action $pkg
	fi
	msg_normal "Exiting from the chroot on $XBPS_MASTERDIR."
	umount_chroot_fs
}

mount_chroot_fs()
{
	local cnt=

	REQFS="sys proc dev xbps xbps_builddir xbps_destdir \
	       xbps_packagesdir xbps_srcdistdir"
	if [ -d "$XBPS_CROSS_DIR" ]; then
		local cross=yes
		REQFS="$REQFS xbps_crossdir"
	fi

	for f in ${REQFS}; do
		if [ ! -f $XBPS_MASTERDIR/.${f}_mount_bind_done ]; then
			echo -n "=> Mounting $f in chroot... "
			local blah=
			case $f in
				xbps) blah=$XBPS_DISTRIBUTIONDIR;;
				xbps_builddir) blah=$XBPS_BUILDDIR;;
				xbps_destdir) blah=$XBPS_DESTDIR;;
				xbps_srcdistdir) blah=$XBPS_SRCDISTDIR;;
				xbps_packagesdir) blah=$XBPS_PACKAGESDIR;;
				xbps_crossdir)
					[ -n $cross ] && blah=$XBPS_CROSS_DIR
					;;
				*) blah=/$f;;
			esac
			[ ! -d $blah ] && echo "failed." && continue
			mount --bind $blah $XBPS_MASTERDIR/$f
			if [ $? -eq 0 ]; then
				echo 1 > $XBPS_MASTERDIR/.${f}_mount_bind_done
				echo "done."
			else
				echo "failed."
			fi
		else
			cnt=$(cat $XBPS_MASTERDIR/.${f}_mount_bind_done)
			cnt=$(($cnt + 1))
			echo $cnt > $XBPS_MASTERDIR/.${f}_mount_bind_done
		fi
	done
	unset f
}

umount_chroot_fs()
{
	local fs=
	local dir=
	local cnt=

	for fs in ${REQFS}; do
		[ ! -f $XBPS_MASTERDIR/.${fs}_mount_bind_done ] && continue
		cnt=$(cat $XBPS_MASTERDIR/.${fs}_mount_bind_done)
		if [ $cnt -gt 1 ]; then
			cnt=$(($cnt - 1))
			echo $cnt > $XBPS_MASTERDIR/.${fs}_mount_bind_done
		else
			echo -n "=> Unmounting $fs from chroot... "
			umount -f $XBPS_MASTERDIR/$fs
			if [ $? -eq 0 ]; then
				rm -f $XBPS_MASTERDIR/.${fs}_mount_bind_done
				echo "done."
			else
				echo "failed."
			fi
		fi
		unset fs
	done

	for dir in ${EXTDIRS}; do
		[ -f $XBPS_MASTERDIR/.${dir}_mount_bind_done ] && continue
		[ -d $XBPS_MASTERDIR/$dir ] && rmdir $XBPS_MASTERDIR/$dir
	done
}

[ -n "$base_chroot" ] && return 0

. $XBPS_SHUTILSDIR/builddep_funcs.sh
check_installed_pkg xbps-base-chroot-0.1
if [ $? -ne 0 ]; then
	echo "The '$pkgname' package requires to be installed in a chroot."
	echo "Please install the 'xbps-base-chroot' package and try again."
	exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
	if [ -n "$origin_tmpl" ]; then
		. $XBPS_SHUTILSDIR/tmpl_funcs.sh
		reset_tmpl_vars
		run_file $XBPS_TEMPLATESDIR/$origin_tmpl/template
	fi
	echo "The '$pkgname' package requires to be installed in a chroot."
	echo "You cannot do this as normal user, try again being root."
	exit 1
fi

msg_normal "Entering into the chroot on $XBPS_MASTERDIR."

EXTDIRS="xbps xbps_builddir xbps_destdir xbps_packagesdir \
	 xbps_srcdistdir xbps_crossdir"
REQDIRS="bin sbin tmp var sys proc dev usr/local/etc ${EXTDIRS}"
for f in ${REQDIRS}; do
	[ ! -d $XBPS_MASTERDIR/$f ] && mkdir -p $XBPS_MASTERDIR/$f
done
unset f REQDIRS

XBPSSRC_CF=$XBPS_MASTERDIR/usr/local/etc/xbps-src.conf

echo "XBPS_DISTRIBUTIONDIR=/xbps" > $XBPSSRC_CF
echo "XBPS_MASTERDIR=/" >> $XBPSSRC_CF
echo "XBPS_DESTDIR=/xbps_destdir" >> $XBPSSRC_CF
echo "XBPS_PACKAGESDIR=/xbps_packagesdir" >> $XBPSSRC_CF
echo "XBPS_BUILDDIR=/xbps_builddir" >> $XBPSSRC_CF
echo "XBPS_SRCDISTDIR=/xbps_srcdistdir" >> $XBPSSRC_CF
echo "XBPS_CFLAGS=\"$XBPS_CFLAGS\"" >> $XBPSSRC_CF
echo "XBPS_CXXFLAGS=\"\$XBPS_CFLAGS\"" >> $XBPSSRC_CF
echo "XBPS_FETCH_CMD=$XBPS_FETCH_CMD" >> $XBPSSRC_CF
if [ -n "$XBPS_MAKEJOBS" ]; then
	echo "XBPS_MAKEJOBS=$XBPS_MAKEJOBS" >> $XBPSSRC_CF
fi
if [ -n "$XBPS_CROSS_TARGET" -a -d "$XBPS_CROSS_DIR" ]; then
	echo "XBPS_CROSS_TARGET=$XBPS_CROSS_TARGET" >> $XBPSSRC_CF
	echo "XBPS_CROSS_DIR=/xbps_crossdir" >> $XBPSSRC_CF
fi



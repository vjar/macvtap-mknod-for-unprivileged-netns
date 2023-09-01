bin_PROGRAMS = macvtap-mknod-for-unprivileged-netns
macvtap_mknod_for_unprivileged_netns_SOURCES = src/main.c
dist_bin_SCRIPTS = macvtap-mknod-targetpid-discovery-podman

install-exec-hook:
	ln -s macvtap-mknod-targetpid-discovery-podman $(DESTDIR)$(bindir)/macvtap-mknod-targetpid-discovery
	setcap CAP_MKNOD=ep $(DESTDIR)$(bindir)/macvtap-mknod-for-unprivileged-netns

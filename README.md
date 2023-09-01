# macvtap (chrdevs) for unprivileged network namespaces

## Example

The default `macvtap-mknod-targetpid-discovery` method allows creating chrdevs
for macvtap interfaces in Podman's rootless network namespaces.

Enter the rootless network namespace and create a macvtap interface.
```
$ podman unshare --rootless-netns
# ip link add link eth0 name mytap address "a0:b1:c2:a1:b2:c3" type macvtap mode bridge
```

Create the chrdev.
```
$ macvtap-mknod-for-unprivileged-netns mytap
```


## Security considerations

The users with execution permission to the `CAP_MKNOD`-equipped binary may
create user-owned chrdevs equipped with their major and minor in [the current
macvtap chrdev
region.](https://www.kernel.org/doc/html/v6.5/core-api/kernel-api.html#c.register_chrdev_region)
The mknod destination path is not forced into a devtmpfs, and they are not
cleared by a reboot.

Creating a large number of macvtap interfaces, their corresponding chrdevs, and
removing the original interfaces afterwards may allow access to subsequent
macvtaps created by other users.


## Installation

Either download and extract a release, or prepare the cloned repo with
`autoreconf --install`, and follow the release installation.

From releases
```
./configure
make install
```

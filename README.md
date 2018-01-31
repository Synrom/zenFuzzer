Zen 2.0.9-4
==============

What is Zen?
----------------
A globally accessible and anonymous blockchain for censorship-resistant communications and economic activity.

1. Get dependencies:
    1. Debian
    ```{r, engine='bash'}
    sudo apt-get install \
          build-essential pkg-config libc6-dev m4 g++-multilib \
          autoconf libtool ncurses-dev unzip git python \
          zlib1g-dev wget bsdmainutils automake
    ```
    2. Centos:
    ```{r, engine='bash')
    sudo yum install epel-release
    sudo yum group install 'Development Tools'
    sudo yum install lbzip2 \
        ncurses-dev unzip python \
        zlib1g-dev wget bsdmainutils
    ```   
    3. Windows
    ```{r, engine='bash'}
    sudo apt-get install \
        build-essential pkg-config libc6-dev m4 g++-multilib \
        autoconf libtool ncurses-dev unzip git python \
        zlib1g-dev wget bsdmainutils automake mingw-w64
    ```

1. Install for linux
```{r, engine='bash'}
# Build
./zcutil/build.sh -j$(nproc)
# fetch key
./zcutil/fetch-params.sh
# Run
./src/zend
```

2. Install for Windows (Cross-Compiled, building on Windows is not supported yet)

```./zcutil/build-win.sh -j$(nproc)```


Instructions to redeem pre block 110,000 ZCL
-------------
1. Linux:
Copy and paste your wallet.dat from ~/.zclassic/ to ~/.zen. That's it!

2. Windows:
Copy and paste your wallet.dat from %APPDATA%/Zclassic/ to %APPDATA%/Zen. That's it!

About
--------------

[Zen](https://zencash.io/) is a platform for secure communications and for deniable economic activity.
Zen is an evolution of the Zclassic codebase aimed at primarily enabling intriniscally secure communications and 
resilient networking. 

This software is the Zen client. It downloads and stores the entire history
of Zen transactions; depending on the speed of your computer and network
connection, the synchronization process could take a day or more once the
blockchain has reached a significant size.

Security Warnings
-----------------

See important security warnings in
[doc/security-warnings.md](doc/security-warnings.md).

**Zen is unfinished and highly experimental.** Use at your own risk.

Where do I begin?
-----------------
* The easiest way to get started is to download one of the available graphical wallets from [zensystem.io](https://zensystem.io)

### Need Help?

* Many guides and tutorials are available at [ZencashOfficial slack](https://slackinvite.zensystem.io/)
  for help and more information.
* Ask for help on the [ZencashOfficial slack](slackinvite.zensystem.io).

### Want to participate in development?

* Code review is welcome!
* If you want to get to know us join our [Slack](https://slackinvite.zensystem.io)


Participation in the Zen project is subject to a
[Code of Conduct](code_of_conduct.md).

Building
--------

Build Zen along with most dependencies from source by running
./zcutil/build.sh for Linux.
./zcutil/build-win.sh for Windows
./zcutil/build-mac.sh for MacOS.

License
-------

For license information see the file [COPYING](COPYING).

package=openssl
$(package)_version=1.1.0h
$(package)_download_path=https://www.openssl.org/source
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=5835626cde9e99656585fc7aaa2302a73a7e1340bf8c14fd635a62c66802a517

define $(package)_set_vars
$(package)_config_env=AR="$($(package)_ar)" RANLIB="$($(package)_ranlib)" CC="$($(package)_cc)"
$(package)_config_opts=--prefix=$(host_prefix) --openssldir=$(host_prefix)/etc/openssl
$(package)_config_opts+=no-afalgeng
$(package)_config_opts+=no-asm
$(package)_config_opts+=no-async
$(package)_config_opts+=no-bf
$(package)_config_opts+=no-blake2
$(package)_config_opts+=no-camellia
# ZEN_MOD_START
#$(package)_config_opts+=no-capieng
# ZEN_MOD_END
$(package)_config_opts+=no-cast
$(package)_config_opts+=no-chacha
$(package)_config_opts+=no-cmac
$(package)_config_opts+=no-cms
# ZEN_MOD_START
#$(package)_config_opts+=no-comp
# ZEN_MOD_END
$(package)_config_opts+=no-crypto-mdebug
$(package)_config_opts+=no-crypto-mdebug-backtrace
# ZEN_MOD_START
#$(package)_config_opts+=no-ct
# ZEN_MOD_END
$(package)_config_opts+=no-des
$(package)_config_opts+=no-dgram
# ZEN_MOD_START
#$(package)_config_opts+=no-dsa
# ZEN_MOD_END
$(package)_config_opts+=no-dso
$(package)_config_opts+=no-dtls
$(package)_config_opts+=no-dtls1
$(package)_config_opts+=no-dtls1-method
$(package)_config_opts+=no-dynamic-engine
# ZEN_MOD_START
#$(package)_config_opts+=no-ec2m
#$(package)_config_opts+=no-ec_nistp_64_gcc_128
# ZEN_MOD_END
$(package)_config_opts+=no-egd
$(package)_config_opts+=no-engine
# ZEN_MOD_START
#$(package)_config_opts+=no-err
# ZEN_MOD_END
$(package)_config_opts+=no-gost
$(package)_config_opts+=no-heartbeats
# ZEN_MOD_START
#$(package)_config_opts+=no-idea
# ZEN_MOD_END
$(package)_config_opts+=no-md2
$(package)_config_opts+=no-md4
$(package)_config_opts+=no-mdc2
$(package)_config_opts+=no-multiblock
$(package)_config_opts+=no-nextprotoneg
$(package)_config_opts+=no-ocb
# ZEN_MOD_START
#$(package)_config_opts+=no-ocsp
# ZEN_MOD_END
$(package)_config_opts+=no-poly1305
# ZEN_MOD_START
#$(package)_config_opts+=no-posix-io
# ZEN_MOD_END
$(package)_config_opts+=no-psk
$(package)_config_opts+=no-rc2
$(package)_config_opts+=no-rc4
$(package)_config_opts+=no-rc5
$(package)_config_opts+=no-rdrand
$(package)_config_opts+=no-rfc3779
$(package)_config_opts+=no-rmd160
$(package)_config_opts+=no-scrypt
$(package)_config_opts+=no-sctp
$(package)_config_opts+=no-seed
$(package)_config_opts+=no-shared
# ZEN_MOD_START
#$(package)_config_opts+=no-sock
# ZEN_MOD_END
$(package)_config_opts+=no-srp
$(package)_config_opts+=no-srtp
$(package)_config_opts+=no-ssl
$(package)_config_opts+=no-ssl3
$(package)_config_opts+=no-ssl3-method
$(package)_config_opts+=no-ssl-trace
# ZEN_MOD_START
#$(package)_config_opts+=no-stdio
#$(package)_config_opts+=no-tls
#$(package)_config_opts+=no-tls1
#$(package)_config_opts+=no-tls1-method
# ZEN_MOD_END
$(package)_config_opts+=no-ts
$(package)_config_opts+=no-ui
$(package)_config_opts+=no-unit-test
$(package)_config_opts+=no-weak-ssl-ciphers
$(package)_config_opts+=no-whirlpool
# ZEN_MOD_START
#$(package)_config_opts+=no-zlib
#$(package)_config_opts+=no-zlib-dynamic
# ZEN_MOD_END
$(package)_config_opts+=$($(package)_cflags) $($(package)_cppflags)
$(package)_config_opts+=-DPURIFY
$(package)_config_opts_linux=-fPIC -Wa,--noexecstack
$(package)_config_opts_x86_64_linux=linux-x86_64
$(package)_config_opts_i686_linux=linux-generic32
$(package)_config_opts_arm_linux=linux-generic32
$(package)_config_opts_aarch64_linux=linux-generic64
$(package)_config_opts_mipsel_linux=linux-generic32
$(package)_config_opts_mips_linux=linux-generic32
$(package)_config_opts_powerpc_linux=linux-generic32
$(package)_config_opts_x86_64_darwin=darwin64-x86_64-cc
$(package)_config_opts_x86_64_mingw32=mingw64
$(package)_config_opts_i686_mingw32=mingw
endef

define $(package)_preprocess_cmds
  sed -i.old "/define DATE/d" util/mkbuildinf.pl && \
  sed -i.old "s|\"engines\", \"apps\", \"test\"|\"engines\"|" Configure
endef

define $(package)_config_cmds
  ./Configure $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE) -j1 build_libs libcrypto.pc libssl.pc openssl.pc
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) -j1 install_sw
endef

define $(package)_postprocess_cmds
  rm -rf share bin etc
endef

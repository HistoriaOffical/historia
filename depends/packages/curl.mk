package=curl
$(package)_version=7.35.0
$(package)_download_path=http://archive.ubuntu.com/ubuntu/pool/main/c/curl
$(package)_file_name=$(package)_$($(package)_version).orig.tar.gz
$(package)_sha256_hash=917d118fc5d61e9dd1538d6519bd93bbebf2e866882419781c2e0fdb2bc42121

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

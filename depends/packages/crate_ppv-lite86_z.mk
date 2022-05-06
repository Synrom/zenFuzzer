package=crate_ppv-lite86_z
$(package)_crate_name=ppv-lite86
$(package)_version=0.2.16
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=eb9f9e6e233e5c4a35559a617bf40a4ec447db2e84c20b55a6f83167b7e57872
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef

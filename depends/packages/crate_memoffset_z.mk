package=crate_memoffset_z
$(package)_crate_name=memoffset
$(package)_version=0.6.5
$(package)_download_path=https://static.crates.io/crates/$($(package)_crate_name)
$(package)_file_name=$($(package)_crate_name)-$($(package)_version).crate
$(package)_sha256_hash=5aa361d4faea93603064a027415f07bd8e1d5c88c9fbf68bf56a285428fd79ce
$(package)_crate_versioned_name=$($(package)_crate_name)

define $(package)_preprocess_cmds
  $(call generate_crate_checksum,$(package))
endef

define $(package)_stage_cmds
  $(call vendor_crate_source,$(package))
endef

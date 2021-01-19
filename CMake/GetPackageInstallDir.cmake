function(GetPackageInstallDir var name version)
	set(${var} "${PROJECT_BINARY_DIR}/${name}_${version}_install" PARENT_SCOPE)
endfunction()

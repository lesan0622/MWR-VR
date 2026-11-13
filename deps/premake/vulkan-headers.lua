vulkan_headers = {
	source = path.join(dependencies.basePath, "Vulkan-Headers"),
}

function vulkan_headers.import()
	vulkan_headers.includes()
end

function vulkan_headers.includes()
	includedirs {
		path.join(vulkan_headers.source, "include")
	}
end

function vulkan_headers.project()
end

table.insert(dependencies, vulkan_headers)

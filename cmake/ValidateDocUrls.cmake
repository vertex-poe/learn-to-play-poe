# Scans C++ sources for docSource("label", "path") calls and verifies
# docs/path.md exists. Invoked as a pre-build custom target; fails the build
# if any path is stale. Has no effect on the shipped binary.

file(GLOB_RECURSE source_files
    "${SOURCE_DIR}/src/*.cpp"
    "${SOURCE_DIR}/src/*.h"
)

set(found_error FALSE)

foreach(src IN LISTS source_files)
    file(STRINGS "${src}" lines REGEX "docSource\\(")
    foreach(line IN LISTS lines)
        # Match docSource("label", "path") — capture the second string argument
        if(line MATCHES "docSource\\(\"[^\"]*\",[ \t]*\"([^\"]+)\"\\)")
            set(doc_path "${CMAKE_MATCH_1}")
            if(NOT EXISTS "${SOURCE_DIR}/docs/${doc_path}.md")
                message(WARNING
                    "${src}: docSource(..., \"${doc_path}\") — "
                    "docs/${doc_path}.md does not exist")
                set(found_error TRUE)
            endif()
        endif()
    endforeach()
endforeach()

if(found_error)
    message(FATAL_ERROR
        "One or more docSource paths are invalid — see warnings above.")
endif()

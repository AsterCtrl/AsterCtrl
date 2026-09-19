function(aster_target_defaults target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    if(ASTER_ENABLE_SANITIZERS)
      target_compile_options(${target} PRIVATE
        -fno-omit-frame-pointer -fsanitize=address,undefined)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    elseif(ASTER_ENABLE_TSAN)
      target_compile_options(${target} PRIVATE
        -fno-omit-frame-pointer -fsanitize=thread)
      target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
  endif()
endfunction()

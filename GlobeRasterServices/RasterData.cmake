include("${CMAKE_CURRENT_LIST_DIR}/cmake/AcquireSampleData.cmake")
geokernel_acquire_sample_data(sagrada_familia_ortophoto.zip
    43a09656ca07490e91a48b413428e8c093d392d3a63549e997c9b4b20ffa9f40
    "${CMAKE_BINARY_DIR}/data")
geokernel_acquire_sample_data(sagrada_familia_terrain.zip
    69ed86c686b2500a70921920387afcb8993f3fe042e2a682932f86b7c6d5127c
    "${CMAKE_BINARY_DIR}/data")
geokernel_deploy_sample_data(GlobeRasterServices)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/AcquireSampleData.cmake")
geokernel_acquire_sample_data(sagrada_familia_terrain.zip
    69ed86c686b2500a70921920387afcb8993f3fe042e2a682932f86b7c6d5127c
    "${CMAKE_BINARY_DIR}/data")
geokernel_deploy_sample_data(GlobeTerrainAnalysis)

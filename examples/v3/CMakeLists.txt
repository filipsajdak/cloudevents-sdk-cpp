# The v0.5.0 examples, spelled against ce::v3 (SWR-BUILD-0013). A v0.5.0
# consumer's code is what these stand for, so they must keep building and running.

ce_add_example(v3_custom_codec SOURCES custom_codec.cpp)

if(CE_CODEC_NLOHMANN)
  ce_add_example(v3_produce SOURCES produce.cpp LIBS ce::codec_nlohmann)
  ce_add_example(v3_consume SOURCES consume.cpp LIBS ce::codec_nlohmann)
  ce_add_example(v3_described_payload SOURCES described_payload.cpp LIBS ce::codec_nlohmann)
endif()

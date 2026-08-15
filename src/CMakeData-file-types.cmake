list(APPEND SMDATA_FILE_TYPES_SRC
            "IniFile.cpp"
            "MsdFile.cpp"
            "XmlFile.cpp"
            "XmlToLua.cpp"
            "XmlFileUtil.cpp")
list(APPEND SMDATA_FILE_TYPES_HPP
            "IniFile.h"
            "MsdFile.h"
            "XmlFile.h"
            "XmlToLua.h"
            "XmlFileUtil.h")

source_group("File Types"
             FILES
             ${SMDATA_FILE_TYPES_SRC}
             ${SMDATA_FILE_TYPES_HPP})

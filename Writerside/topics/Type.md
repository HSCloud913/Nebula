# Type

Nebula project macro, type re-define

## Macro Lists

### BEGIN_NS
namespace 를 간소화한 매크로이며 네임스페이스 중첩이 가능합니다.

### END_NS
namespace 를 닫는 매크로 입니다.

### NEBULA_API
일반적인 프로젝트일 경우 아무 동작도 하지 않으며, dll 프로젝트 일 경우 export 하기 위한 매크로 입니다.

### NOT_BUILD_NEBULA_DEPRECATE
사용하지 않는 함수를 deprecated 하기 위한 매크로 입니다.

### NEBULA_DEFAULT_MOVE
클래스의 기본 복사 생성자와 기본 복사 대입 연산자를 정의하는 매크로 입니다. 

### NEBULA_NON_COPYABLE
클래스의 복사를 허용하지 않는 매크로 입니다.

### NEBULA_NON_MOVABLE
클래스의 이동을 허용하지 않는 매크로 입니다.

### NEBULA_NON_COPYABLE_MOVABLE
클래스의 복사와 이동을 허용하지 않는 매크로 입니다.

## Type Lists

<table style="header-column">
    <tr>
        <td>std::string</td>
        <td>Ne::string_t</td>
    </tr>
    <tr>
        <td>std::string_view</td>
        <td>Ne::string_view_t</td>
    </tr>
    <tr>
        <td>std::wstring</td>
        <td>Ne::wstring_t</td>
    </tr>
    <tr>
        <td>std::wstring_view</td>
        <td>Ne::wstring_view_t</td>
    </tr>
    <tr>
        <td>char*</td>
        <td>lpstr_t</td>
    </tr>
    <tr>
        <td>const char*</td>
        <td>lpcstr_t</td>
    </tr>
    <tr>
        <td>wchar_t*</td>
        <td>lpwstr_t</td>
    </tr>
    <tr>
        <td>const wchar_t*</td>
        <td>lpcwstr_t</td>
    </tr>
    <tr>
        <td>char</td>
        <td>char_t</td>
    </tr>
    <tr>
        <td>unsigned char</td>
        <td>byte_t</td>
    </tr>
    <tr>
        <td>short</td>
        <td>short_t, int16_t</td>
    </tr>
    <tr>
        <td>unsigned short</td>
        <td>ushort_t, uint16_t, word_t</td>
    </tr>
    <tr>
        <td>int</td>
        <td>int_t</td>
    </tr>
    <tr>
        <td>unsigned int</td>
        <td>uint_t</td>
    </tr>
    <tr>
        <td>long</td>
        <td>long_t</td>
    </tr>
    <tr>
        <td>unsigned long</td>
        <td>ulong_t, dword_t</td>
    </tr>
    <tr>
        <td>long long</td>
        <td>longlong_t</td>
    </tr>
    <tr>
        <td>unsigned long long</td>
        <td>ulonglong_t, dwordlong_t</td>
    </tr>
    <tr>
        <td>float</td>
        <td>float_t</td>
    </tr>
    <tr>
        <td>double</td>
        <td>double_t</td>
    </tr>
    <tr>
        <td>bool</td>
        <td>bool_t</td>
    </tr>
    <tr>
        <td>void</td>
        <td>void_t</td>
    </tr>
</table>

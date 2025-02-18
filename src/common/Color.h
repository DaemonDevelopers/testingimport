/*
===========================================================================
Daemon BSD Source Code
Copyright (c) 2013-2016, Daemon Developers
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Daemon developers nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL DAEMON DEVELOPERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
===========================================================================
*/

#ifndef COMMON_COLOR_H_
#define COMMON_COLOR_H_

#include <limits>
#include <string>
#include <type_traits>

#include "Compiler.h"
#include "Math.h"

#define convertFromSRGB( v ) (v <= 0.04045f ? v * (1.0f / 12.92f) : pow((v + 0.055f) * (1.0f / 1.055f), 2.4f))

namespace Color {

/*
 * Template class to get information on color components
 */
template<class T>
	struct ColorComponentTraits
{
	// Type used to represent the components
	using component_type = T;
	// Maximum value for a component
	static CONSTEXPR component_type component_max = std::numeric_limits<component_type>::max();
	// Size of a component value in bytes
	static CONSTEXPR std::size_t component_size = sizeof(component_type);
};
/*
 * Specialization for normalized floats
 */
template<>
	struct ColorComponentTraits<float>
{
	using component_type = float;
	static CONSTEXPR int            component_max = 1;
	static CONSTEXPR std::size_t    component_size = sizeof(component_type);
};

/*
 * ColorAdaptor to convert different representations of RGB(A) colors to BasicColor
 *
 * Specializations must provide the following members:
 * 	static (AdaptedType) Adapt( TemplateType ); // Creates an object matching the Color concept
 *
 * Requirements for AdaptedType:
 * 	static bool is_color = true;
 * 	using component_type = (unspecified); // See ColorComponentTraits
 * 	static component_type component_max = (unspecified); // See ColorComponentTraits
 * 	component_type Red()   const; // Red component
 * 	component_type Green() const; // Green component
 * 	component_type Blue()  const; // Blue component
 * 	component_type Alpha() const; // Alpha component
 *
 */
template<class T>
class ColorAdaptor;

/*
 * Specializations for arrays
 * Assumes it has 4 Component
 */
template<class Component>
class ColorAdaptor<Component*>
{
public:
	static CONSTEXPR bool is_color = true;
	using component_type = Component;
	static CONSTEXPR auto component_max = ColorComponentTraits<Component>::component_max;

	static ColorAdaptor Adapt( const Component* array )
	{
		return ColorAdaptor( array );
	}

	ColorAdaptor( const Component* array ) : array( array ) {}

	Component Red() const { return array[ 0 ]; }
	Component Green() const { return array[ 1 ]; }
	Component Blue() const { return array[ 2 ]; }
	Component Alpha() const { return array[ 3 ]; }

private:
	const Component* array;
};

template<class Component>
class ColorAdaptor<Component[4]> : public ColorAdaptor<Component*>
{};

template<class Component>
class ColorAdaptor<Component[3]> : public ColorAdaptor<Component*>
{
public:
	static ColorAdaptor Adapt( const Component array[3] )
	{
		return ColorAdaptor( array );
	}

	ColorAdaptor( const Component array[3] ) : ColorAdaptor<Component*>( array ) {}

	Component Alpha() const { return ColorAdaptor<Component*>::component_max; }
};

/*
 * Creates an adaptor for the given value
 * T must have a proper specialization of ColorAdaptor
 */
template<class T>
auto Adapt( const T& object ) -> decltype( ColorAdaptor<T>::Adapt( object ) )
{
	return ColorAdaptor<T>::Adapt( object );
}

template<class Component, class Traits> class BasicColor;
namespace detail {
const BasicColor<float,ColorComponentTraits<float>>& Indexed(int index);
} // namespace detail

// A color with RGBA components
template<class Component, class Traits = ColorComponentTraits<Component>>
class BasicColor
{
public:
	static CONSTEXPR bool is_color = true;
	using color_traits = Traits;
	using component_type = typename color_traits::component_type;
	static CONSTEXPR auto component_max = color_traits::component_max;

	// Returns the value of an indexed color
	static BasicColor Indexed( int i )
    {
        return detail::Indexed( i );
    }

	// Initialize from the components
	CONSTEXPR_FUNCTION BasicColor( component_type r, component_type g, component_type b,
	       component_type a = component_max ) NOEXCEPT
		: data_{ r, g, b, a }
	{}

    // Default constructor, all components set to zero
    CONSTEXPR_FUNCTION BasicColor() NOEXCEPT = default;

	CONSTEXPR_FUNCTION BasicColor( const BasicColor& ) NOEXCEPT = default;
	CONSTEXPR_FUNCTION BasicColor( BasicColor&& ) NOEXCEPT = default;
    BasicColor& operator=( const BasicColor& ) NOEXCEPT = default;
    BasicColor& operator=( BasicColor&& ) NOEXCEPT = default;

	template<class T, class = std::enable_if<T::is_color>>
		BasicColor( const T& adaptor ) : data_{
			ConvertComponent<T>( adaptor.Red() ),
			ConvertComponent<T>( adaptor.Green() ),
			ConvertComponent<T>( adaptor.Blue() ),
			ConvertComponent<T>( adaptor.Alpha() ) }
		{}


	template<class T, class = std::enable_if<T::is_color>>
		BasicColor& operator=( const T& adaptor )
		{
			SetRed( ConvertComponent<T>( adaptor.Red() ) );
			SetGreen( ConvertComponent<T>( adaptor.Green() ) );
			SetBlue( ConvertComponent<T>( adaptor.Blue() ) );
			SetAlpha( ConvertComponent<T>( adaptor.Alpha() ) );

			return *this;
		}

	// Converts to an array
	CONSTEXPR_FUNCTION const component_type* ToArray() const NOEXCEPT
	{
		return data_;
	}

	CONSTEXPR_FUNCTION_RELAXED component_type* ToArray() NOEXCEPT
	{
		return data_;
	}

	void ToArray( component_type* output ) const
	{
		std::copy_n( ToArray(), 4, output );
	}

	// Size of the memory location returned by ToArray() in bytes
	CONSTEXPR_FUNCTION std::size_t ArrayBytes() const NOEXCEPT
	{
		return sizeof(data_);
	}

	CONSTEXPR_FUNCTION component_type Red() const NOEXCEPT
	{
		return data_[ 0 ];
	}

	CONSTEXPR_FUNCTION component_type Green() const NOEXCEPT
	{
		return data_[ 1 ];
	}

	CONSTEXPR_FUNCTION component_type Blue() const NOEXCEPT
	{
		return data_[ 2 ];
	}

	CONSTEXPR_FUNCTION component_type Alpha() const NOEXCEPT
	{
		return data_[ 3 ];
	}

	CONSTEXPR_FUNCTION_RELAXED void SetRed( component_type v ) NOEXCEPT
	{
		data_[ 0 ] = v;
	}

	CONSTEXPR_FUNCTION_RELAXED void SetGreen( component_type v ) NOEXCEPT
	{
		data_[ 1 ] = v;
	}

	CONSTEXPR_FUNCTION_RELAXED void SetBlue( component_type v ) NOEXCEPT
	{
		data_[ 2 ] = v;
	}

	CONSTEXPR_FUNCTION_RELAXED void SetAlpha( component_type v ) NOEXCEPT
	{
		data_[ 3 ] = v;
	}

	CONSTEXPR_FUNCTION_RELAXED component_type ConvertFromSRGB( component_type v ) NOEXCEPT
	{
		float f = float( v ) / 255.0f;
		f = convertFromSRGB( f );
		return component_type( f * 255 );
	}

	CONSTEXPR_FUNCTION_RELAXED void ConvertFromSRGB() NOEXCEPT
	{
		SetRed( ConvertFromSRGB( Red() ) );
		SetGreen( ConvertFromSRGB( Green() ) );
		SetBlue( ConvertFromSRGB( Blue() ) );
	}

	CONSTEXPR_FUNCTION_RELAXED BasicColor& operator*=( float factor ) NOEXCEPT
	{
		*this = *this * factor;
		return *this;
	}

	// FIXME: multiplying both rgb AND alpha by something doesn't seem like an operation anyone would want
	CONSTEXPR_FUNCTION BasicColor operator*( float factor ) const NOEXCEPT
	{
		return BasicColor( Red() * factor, Green() * factor, Blue() * factor, Alpha() * factor );
	}

	// Fits the component values from 0 to component_max
	CONSTEXPR_FUNCTION_RELAXED void Clamp()
	{
		SetRed( Math::Clamp<component_type>( Red(), component_type(), component_max ) );
		SetGreen( Math::Clamp<component_type>( Green(), component_type(), component_max ) );
		SetBlue( Math::Clamp<component_type>( Blue(), component_type(), component_max ) );
		SetAlpha( Math::Clamp<component_type>( Alpha(), component_type(), component_max ) );
	}

private:
	// Converts a component, used by conversions from classes implementing the Color concepts
	template<class T>
	static CONSTEXPR_FUNCTION
		typename std::enable_if<component_max != T::component_max, component_type>::type
			ConvertComponent( typename T::component_type from ) NOEXCEPT
	{
		using work_type = typename std::common_type<
			component_type,
			typename T::component_type
		>::type;

		return work_type( from )  / work_type( T::component_max ) * work_type( component_max );
	}
	// Specialization for when the value shouldn't change
	template<class T>
	static CONSTEXPR_FUNCTION
		typename std::enable_if<component_max == T::component_max, component_type>::type
			ConvertComponent( typename T::component_type from ) NOEXCEPT
	{
		return from;
	}

	component_type data_[4]{};
};

using Color = BasicColor<float>;
using Color32Bit = BasicColor<uint8_t>;

/*
 * Blend two colors.
 * If factor is 0, the first color will be shown, it it's 1 the second one will
 */
template<class ComponentType, class Traits = ColorComponentTraits<ComponentType>>
CONSTEXPR_FUNCTION BasicColor<ComponentType, Traits> Blend(
	const BasicColor<ComponentType, Traits>& a,
	const BasicColor<ComponentType, Traits>& b,
	float factor ) NOEXCEPT
{
	return BasicColor<ComponentType, Traits> {
		ComponentType ( a.Red()   * ( 1 - factor ) + b.Red()   * factor ),
		ComponentType ( a.Green() * ( 1 - factor ) + b.Green() * factor ),
		ComponentType ( a.Blue()  * ( 1 - factor ) + b.Blue()  * factor ),
		ComponentType ( a.Alpha() * ( 1 - factor ) + b.Alpha() * factor ),
	};
}

namespace detail {

std::string ToString( const Color32Bit& color );

} // namespace detail

/**
 * Returns a C string for the given color, suitable for printf-like functions
 */
template<class Component, class Traits = ColorComponentTraits<Component>>
std::string ToString( const BasicColor<Component, Traits>& color )
{
	return detail::ToString( color );
}

namespace Constants {
// Namespace enum to have these constants scoped but allowing implicit conversions
enum {
	ESCAPE = '^',
	NULL_COLOR = '*',
}; // enum
} // namespace Constants

const Color Black    = { 0.00, 0.00, 0.00, 1.00 };
const Color Red      = { 1.00, 0.00, 0.00, 1.00 };
const Color Green    = { 0.00, 1.00, 0.00, 1.00 };
const Color Blue     = { 0.00, 0.00, 1.00, 1.00 };
const Color Yellow   = { 1.00, 1.00, 0.00, 1.00 };
const Color Orange   = { 1.00, 0.50, 0.00, 1.00 };
const Color Magenta  = { 1.00, 0.00, 1.00, 1.00 };
const Color Cyan     = { 0.00, 1.00, 1.00, 1.00 };
const Color White    = { 1.00, 1.00, 1.00, 1.00 };
const Color LtGrey   = { 0.75, 0.75, 0.75, 1.00 };
const Color MdGrey   = { 0.50, 0.50, 0.50, 1.00 };
const Color LtOrange = { 0.50, 0.25, 0.00, 1.00 };

/*
 * Token for parsing colored strings
 */
class Token
{
public:
	enum class TokenType {
		INVALID,       // Invalid/empty token
		CHARACTER,     // A character
		ESCAPE,        // Color escape
		COLOR,         // Color code
	};

	/*
	 * Constructs an invalid token
	 */
	Token() = default;

	/*
	 * Constructs a token with the given type and range
	 */
	Token( const char* begin, const char* end, TokenType type )
		: begin( begin ),
		  end( end ),
		  type( type )
	{}

	/*
	 * Constructs a token representing a color
	 */
	Token( const char* begin, const char* end, const ::Color::Color& color )
		: begin( begin ),
		  end( end ),
		  type( TokenType::COLOR ),
		  color( color )
	{}

	/*
	 * RawToken defines the extent of the token as it was written in the original string. It
	 * may have escaping deficiencies (namely an unescaped '^') which make it unsafe to concatenate
	 * with other text. RawBegin/RawEnd point into the original string.
	 *
	 * Prefer RawToken over NormalizedToken when you need pointers to the original string or
	 * if you want to preserve the exact character sequence of the input.
	 */
	Str::StringView RawToken() const
	{
		return { begin, end };
	}

	/*
	 * NormalizedToken provides a normalized version of the token with everything
	 * correctly escaped. To wit, a lone "^" is converted to "^^". May point outside the input
	 * string.
	 */
	Str::StringView NormalizedToken() const
	{
		static const char ESCAPED_ESCAPE[] = { Constants::ESCAPE, Constants::ESCAPE };
		if ( type == TokenType::ESCAPE ) {
			return { std::begin( ESCAPED_ESCAPE ), std::end( ESCAPED_ESCAPE ) };
		}
		return { begin, end };
	}

	// Token Type
	TokenType Type() const
	{
		return type;
	}

	/*
	 * Token as plain text, for contexts where color codes aren't interpreted.
	 * A color token gives the empty string.
	 */
	Str::StringView PlainText() const
	{
		switch (type)
		{
			case TokenType::ESCAPE: return { begin, begin + 1 };
			case TokenType::CHARACTER: return { begin, end };
			default: return {};
		}
	}

	/*
	 * Parsed color
	 * If the token is the reset color code "^*", then the alpha channel is 0 and the RGB comes
	 * from the Parser's default color. Otherwise, the alpha channel is 1 and the RGB comes from
	 * parsing the token.
	 * Pre: Type() == COLOR
	 */
	::Color::Color Color() const
	{
		return color;
	}

	/*
	 * Converts to bool if the token is valid (and not empty)
	 */
	explicit operator bool() const
	{
		return type != TokenType::INVALID && begin && begin < end;
	}

private:

	const char*   begin = nullptr;
	const char*   end   = nullptr;
	TokenType      type  = TokenType::INVALID;
	::Color::Color color;

};

class Parser;

/**
 * Class to parse C-style strings into tokens,
 * implements the InputIterator concept
 */
class TokenIterator
{
public:
	using value_type = Token;
	using reference = const value_type&;
	using pointer = const value_type*;
	using iterator_category = std::input_iterator_tag;
	using difference_type = int;

	TokenIterator() = default;

	explicit TokenIterator( const char* input, const Parser* parent )
        : parent( parent )
	{
		token = NextToken( input );
	}

	reference operator*() const
	{
		return token;
	}

	pointer operator->() const
	{
		return &token;
	}

	TokenIterator& operator++()
	{
		token = NextToken( token.RawToken().end() );
		return *this;
	}

	TokenIterator operator++(int)
	{
		auto copy = *this;
		token = NextToken( token.RawToken().end() );
		return copy;
	}

	bool operator==( const TokenIterator& rhs ) const
	{
		return token.RawToken().begin() == rhs.token.RawToken().begin();
	}

	bool operator!=( const TokenIterator& rhs ) const
	{
		return token.RawToken().begin() != rhs.token.RawToken().begin();
	}

private:
	// Returns the token corresponding to the given input string
	value_type NextToken(const char* input);

	value_type token;
    const Parser* parent = nullptr;
};

/**
 * Class to parse C-style utf-8 strings into tokens
 */
class Parser
{
public:
    using value_type = Token;
    using reference = const value_type&;
    using pointer = const value_type*;
    using iterator = TokenIterator;

    explicit Parser(const char* input, const Color& default_color = White)
        : input(input), default_color(default_color)
    {}

    iterator begin() const
    {
        return iterator(input, this);
    }

    iterator end() const
    {
        return iterator();
    }

    Color DefaultColor() const
    {
        return default_color;
    }

private:
    const char* input;
    Color default_color;
};


// Returns the number of characters in a string discarding color codes
// UTF-8 sequences are counted as a single character
int StrlenNocolor( const char* string );

// Returns the plain text of the possibly color-coded string
std::string StripColors( Str::StringRef string );

// Removes color codes from in, writing its plain text to out
// Pre: in NUL terminated and out can contain at least len characters
void StripColors( const char *in, char *out, size_t len );

} // namespace Color

#endif // COMMON_COLOR_H_

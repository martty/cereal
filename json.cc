/*! \file json.hpp
    \brief JSON input and output archives */
/*
  Copyright (c) 2014, Randolph Voorhies, Shane Grant
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
      * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
      * Neither the name of cereal nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL RANDOLPH VOORHIES OR SHANE GRANT BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "cereal/cereal.hpp"
#include "cereal/details/util.hpp"
#include "cereal/archives/json.hpp"

#include "cereal/external/rapidjson/prettywriter.h"
#include "cereal/external/rapidjson/ostreamwrapper.h"
#include "cereal/external/rapidjson/istreamwrapper.h"
#include "cereal/external/rapidjson/document.h"
#include "cereal/external/base64.hpp"

#include <limits>
#include <sstream>
#include <stack>
#include <vector>
#include <string>

namespace cereal {
	JSONOutputArchive::Options::Options( int precision,
                            IndentChar indentChar,
                            unsigned int indentLength) :
            itsPrecision( precision ),
            itsIndentChar( static_cast<char>(indentChar) ),
            itsIndentLength( indentLength ) { }

	JSONOutputArchive::Options::Options() :
            itsPrecision( JSONWriter::kDefaultMaxDecimalPlaces),
            itsIndentChar( static_cast<char>(IndentChar::space) ),
            itsIndentLength( 4 ) { }

	JSONOutputArchive::Options JSONOutputArchive::Options::Default() { return Options(); }
    JSONOutputArchive::Options JSONOutputArchive::Options::NoIndent(){ return Options( JSONWriter::kDefaultMaxDecimalPlaces, IndentChar::space, 0 ); }

	JSONOutputArchive::JSONOutputArchive(std::ostream & stream, Options const & options) :
        OutputArchive<JSONOutputArchive>(this),
        itsWriteStream(std::make_unique<WriteStream>(stream)),
        itsWriter(std::make_unique<JSONWriter>(*itsWriteStream)),
        itsNextName(nullptr)
      {
        itsWriter->SetMaxDecimalPlaces( options.itsPrecision );
        itsWriter->SetIndent( options.itsIndentChar, options.itsIndentLength );
        itsNameCounter.push(0);
        itsNodeStack.push(NodeType::StartObject);
      }

      //! Destructor, flushes the JSON
      JSONOutputArchive::~JSONOutputArchive() CEREAL_NOEXCEPT
      {
        if (itsNodeStack.top() == NodeType::InObject)
          itsWriter->EndObject();
        else if (itsNodeStack.top() == NodeType::InArray)
          itsWriter->EndArray();
      }

      //! Saves some binary data, encoded as a base64 string, with an optional name
      /*! This will create a new node, optionally named, and insert a value that consists of
          the data encoded as a base64 string */
      void JSONOutputArchive::saveBinaryValue( const void * data, size_t size, const char * name )
      {
        setNextName( name );
        writeName();

        auto base64string = base64::encode( reinterpret_cast<const unsigned char *>( data ), size );
        saveValue( base64string );
      };

      //! @}
      /*! @name Internal Functionality
          Functionality designed for use by those requiring control over the inner mechanisms of
          the JSONOutputArchive */
      //! @{

      //! Starts a new node in the JSON output
      /*! The node can optionally be given a name by calling setNextName prior
          to creating the node

          Nodes only need to be started for types that are themselves objects or arrays */
      void JSONOutputArchive::startNode()
      {
        writeName();
        itsNodeStack.push(NodeType::StartObject);
        itsNameCounter.push(0);
      }

      //! Designates the most recently added node as finished
      void JSONOutputArchive::finishNode()
      {
        // if we ended up serializing an empty object or array, writeName
        // will never have been called - so start and then immediately end
        // the object/array.
        //
        // We'll also end any object/arrays we happen to be in
        switch(itsNodeStack.top())
        {
          case NodeType::StartArray:
            itsWriter->StartArray();
          case NodeType::InArray:
            itsWriter->EndArray();
            break;
          case NodeType::StartObject:
            itsWriter->StartObject();
          case NodeType::InObject:
            itsWriter->EndObject();
            break;
        }

        itsNodeStack.pop();
        itsNameCounter.pop();
      }

      //! Sets the name for the next node created with startNode
      void JSONOutputArchive::setNextName( const char * name )
      {
        itsNextName = name;
      } 

      //! Saves a bool to the current node
      void JSONOutputArchive::saveValue(bool b)                { itsWriter->Bool(b);                                                         }
      //! Saves an int to the current node
      void JSONOutputArchive::saveValue(int i)                 { itsWriter->Int(i);                                                          }
      //! Saves a uint to the current node
      void JSONOutputArchive::saveValue(unsigned u)            { itsWriter->Uint(u);                                                         }
      //! Saves an int64 to the current node
      void JSONOutputArchive::saveValue(int64_t i64)           { itsWriter->Int64(i64);                                                      }
      //! Saves a uint64 to the current node
      void JSONOutputArchive::saveValue(uint64_t u64)          { itsWriter->Uint64(u64);                                                     }
      //! Saves a double to the current node
      void JSONOutputArchive::saveValue(double d)              { itsWriter->Double(d);                                                       }
      //! Saves a string to the current node
      void JSONOutputArchive::saveValue(std::string const & s) { itsWriter->String(s.c_str(), static_cast<CEREAL_RAPIDJSON_NAMESPACE::SizeType>( s.size() )); }
      //! Saves a const char * to the current node
      void JSONOutputArchive::saveValue(char const * s)        { itsWriter->String(s);                                                       }
      //! Saves a nullptr to the current node
      void JSONOutputArchive::saveValue(std::nullptr_t)        { itsWriter->Null();                                                          }

	void JSONOutputArchive::writeName()
      {
        NodeType const & nodeType = itsNodeStack.top();

        // Start up either an object or an array, depending on state
        if(nodeType == NodeType::StartArray)
        {
          itsWriter->StartArray();
          itsNodeStack.top() = NodeType::InArray;
        }
        else if(nodeType == NodeType::StartObject)
        {
          itsNodeStack.top() = NodeType::InObject;
          itsWriter->StartObject();
        }

        // Array types do not output names
        if(nodeType == NodeType::InArray) return;

        if(itsNextName == nullptr)
        {
          std::string name = "value" + std::to_string( itsNameCounter.top()++ ) + "\0";
          saveValue(name);
        }
        else
        {
          saveValue(itsNextName);
          itsNextName = nullptr;
        }
      }

      //! Designates that the current node should be output as an array, not an object
      void JSONOutputArchive::makeArray()
      {
        itsNodeStack.top() = NodeType::StartArray;
      }

}

namespace cereal {
	typedef CEREAL_RAPIDJSON_NAMESPACE::GenericValue<CEREAL_RAPIDJSON_NAMESPACE::UTF8<>> JSONValue;
    typedef JSONValue::ConstMemberIterator MemberIterator;
    typedef JSONValue::ConstValueIterator ValueIterator;
    typedef CEREAL_RAPIDJSON_NAMESPACE::Document::GenericValue GenericValue;

      //! @}
      /*! @name Internal Functionality
          Functionality designed for use by those requiring control over the inner mechanisms of
          the JSONInputArchive */
      //! @{

      //! An internal iterator that handles both array and object types
      /*! This class is a variant and holds both types of iterators that
          rapidJSON supports - one for arrays and one for objects. */
      class Iterator
      {
        public:
          Iterator() : itsIndex( 0 ), itsType(Null_) {}

          Iterator(MemberIterator begin, MemberIterator end) :
            itsMemberItBegin(begin), itsMemberItEnd(end), itsIndex(0), itsType(Member)
          {
            if( std::distance( begin, end ) == 0 )
              itsType = Null_;
          }

          Iterator(ValueIterator begin, ValueIterator end) :
            itsValueItBegin(begin), itsValueItEnd(end), itsIndex(0), itsType(Value)
          {
            if( std::distance( begin, end ) == 0 )
              itsType = Null_;
          }

          //! Advance to the next node
          Iterator & operator++()
          {
            ++itsIndex;
            return *this;
          }

          //! Get the value of the current node
          GenericValue const & value()
          {
            switch(itsType)
            {
              case Value : return itsValueItBegin[itsIndex];
              case Member: return itsMemberItBegin[itsIndex].value;
              default: throw cereal::Exception("JSONInputArchive internal error: null or empty iterator to object or array!");
            }
          }

          //! Get the name of the current node, or nullptr if it has no name
          const char * name() const
          {
            if( itsType == Member && (itsMemberItBegin + itsIndex) != itsMemberItEnd )
              return itsMemberItBegin[itsIndex].name.GetString();
            else
              return nullptr;
          }

          //! Adjust our position such that we are at the node with the given name
          /*! @throws Exception if no such named node exists */
          inline bool search( const char * searchName, bool optional )
          {
            const auto len = std::strlen( searchName );
            size_t index = 0;
            for( auto it = itsMemberItBegin; it != itsMemberItEnd; ++it, ++index )
            {
              const auto currentName = it->name.GetString();
              if( ( std::strncmp( searchName, currentName, len ) == 0 ) &&
                  ( std::strlen( currentName ) == len ) )
              {
                itsIndex = index;
                return true;
              }
            }
			
			if (optional)
				return false;
			else
				throw Exception("JSON Parsing failed - provided NVP (" + std::string(searchName) + ") not found");
          }

		  size_type size() {
			  size_type i = 0;
			  while ((itsMemberItBegin + i) != itsMemberItEnd) {
				  i++;
			  }
			  return i;
		  }

        private:
          MemberIterator itsMemberItBegin, itsMemberItEnd; //!< The member iterator (object)
          ValueIterator itsValueItBegin, itsValueItEnd;    //!< The value iterator (array)
          size_t itsIndex;                                 //!< The current index of this iterator
          enum Type {Value, Member, Null_} itsType;        //!< Whether this holds values (array) or members (objects) or nothing
      };

	JSONInputArchive::JSONInputArchive(std::istream & stream) :
        InputArchive<JSONInputArchive>(this),
        itsNextName( nullptr ),
        itsReadStream(std::make_unique<ReadStream>(stream)),
		itsDocument(std::make_unique<CEREAL_RAPIDJSON_NAMESPACE::GenericDocument<CEREAL_RAPIDJSON_NAMESPACE::UTF8<char>, CEREAL_RAPIDJSON_NAMESPACE::MemoryPoolAllocator<CEREAL_RAPIDJSON_NAMESPACE::CrtAllocator>, CEREAL_RAPIDJSON_NAMESPACE::CrtAllocator>>()),
		itsIteratorStack(std::make_unique<std::vector<Iterator>>())
      {

        itsDocument->ParseStream<>(*itsReadStream);
        if (itsDocument->IsArray())
          itsIteratorStack->emplace_back(itsDocument->Begin(), itsDocument->End());
        else
          itsIteratorStack->emplace_back(itsDocument->MemberBegin(), itsDocument->MemberEnd());
      }

	JSONInputArchive::~JSONInputArchive(){}
	
	void JSONInputArchive::loadBinaryValue( void * data, size_t size, const char * name )
      {
        itsNextName = name;

        std::string encoded;
        loadValue( encoded );
        auto decoded = base64::decode( encoded );

        if( size != decoded.size() )
          throw Exception("Decoded binary data size does not match specified size");

        std::memcpy( data, decoded.data(), decoded.size() );
        itsNextName = nullptr;
      };
      bool JSONInputArchive::search()
      {
		bool success = true;
        // The name an NVP provided with setNextName()
        if( itsNextName )
        {
          // The actual name of the current node
          auto const actualName = itsIteratorStack->back().name();

          // Do a search if we don't see a name coming up, or if the names don't match
          if( !actualName || std::strcmp( itsNextName, actualName ) != 0 )
            success = itsIteratorStack->back().search( itsNextName, optional );
        }

        itsNextName = nullptr;
		return success;
      }

      //! Starts a new node, going into its proper iterator
      /*! This places an iterator for the next node to be parsed onto the iterator stack.  If the next
          node is an array, this will be a value iterator, otherwise it will be a member iterator.

          By default our strategy is to start with the document root node and then recursively iterate through
          all children in the order they show up in the document.
          We don't need to know NVPs to do this; we'll just blindly load in the order things appear in.

          If we were given an NVP, we will search for it if it does not match our the name of the next node
          that would normally be loaded.  This functionality is provided by search(). */
      bool JSONInputArchive::startNode()
      {
		if(!search())
			  return false;

        if(itsIteratorStack->back().value().IsArray())
          itsIteratorStack->emplace_back(itsIteratorStack->back().value().Begin(), itsIteratorStack->back().value().End());
        else
          itsIteratorStack->emplace_back(itsIteratorStack->back().value().MemberBegin(), itsIteratorStack->back().value().MemberEnd());
		return true;
      }

      //! Finishes the most recently started node
      void JSONInputArchive::finishNode()
      {
        itsIteratorStack->pop_back();
        ++itsIteratorStack->back();
      }

      //! Retrieves the current node name
      /*! @return nullptr if no name exists */
      const char * JSONInputArchive::getNodeName() const
      {
        return itsIteratorStack->back().name();
      }

      //! Sets the name for the next node created with startNode
      void JSONInputArchive::setNextName( const char * name )
      {
        itsNextName = name;
      }

	 //! Sets the name for the next node created with startNode
      void JSONInputArchive::setNextOptional( bool optional )
      {
		  this->optional = optional;
      }

      //! Loads a value from the current node - bool overload
	  int64_t JSONInputArchive::loadInt() { int64_t val = itsIteratorStack->back().value().GetInt(); ++itsIteratorStack->back(); return val; }
	  uint64_t JSONInputArchive::loadUint() { uint64_t val = itsIteratorStack->back().value().GetUint(); ++itsIteratorStack->back(); return val; }
      //! Loads a value from the current node - bool overload
      void JSONInputArchive::loadValue(bool & val)        { if(!search()) return; val = itsIteratorStack->back().value().GetBool(); ++itsIteratorStack->back(); }
      //! Loads a value from the current node - int64 overload
      void JSONInputArchive::loadValue(int64_t & val)     { if(!search()) return; val = itsIteratorStack->back().value().GetInt64(); ++itsIteratorStack->back(); }
      //! Loads a value from the current node - uint64 overload
      void JSONInputArchive::loadValue(uint64_t & val)    { if(!search()) return; val = itsIteratorStack->back().value().GetUint64(); ++itsIteratorStack->back(); }
      //! Loads a value from the current node - float overload
      void JSONInputArchive::loadValue(float & val)       { if(!search()) return; val = static_cast<float>(itsIteratorStack->back().value().GetDouble()); ++itsIteratorStack->back(); }
      //! Loads a value from the current node - double overload
      void JSONInputArchive::loadValue(double & val)      { if(!search()) return; val = itsIteratorStack->back().value().GetDouble(); ++itsIteratorStack->back(); }
      //! Loads a value from the current node - string overload
      void JSONInputArchive::loadValue(std::string & val) { if(!search()) return; val = itsIteratorStack->back().value().GetString(); ++itsIteratorStack->back(); }
	  //! Loads a value from the current node - string overload
      void JSONInputArchive::loadValue(char* & val) { if(!search()) return; strcpy(val, itsIteratorStack->back().value().GetString()); ++itsIteratorStack->back(); }

      //! Loads a nullptr from the current node
      void JSONInputArchive::loadValue(std::nullptr_t&)   { if(!search()) return; CEREAL_RAPIDJSON_ASSERT(itsIteratorStack->back().value().IsNull()); ++itsIteratorStack->back(); }
	
	  void JSONInputArchive::loadSize(size_type & size)
      {
        if (itsIteratorStack->size() == 1)
          size = itsDocument->Size();
        else
          size = (itsIteratorStack->rbegin() + 1)->value().Size();
      }

	  size_type JSONInputArchive::size() {
		  return itsIteratorStack->back().size();
	  }

}
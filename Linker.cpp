#include "stdafx.h"
#include "Linker.h"

// -------------------------------------------------------------------------------------
// Library		Homework
// File			Linker.cpp
// Author		Ivan Shapovalov <intelfx100@gmail.com>
// Description	Default linker plugin implementation.
// -------------------------------------------------------------------------------------

namespace
{
	using namespace Processor;

} // unnamed namespace

namespace ProcessorImplementation
{
using namespace Processor;

void UATLinker::DirectLink_HandleReference( Reference& ref, const Offsets& limits )
{
	verify_method;
	msg( E_INFO, E_DEBUG, "(Direct link) handling reference to %s", ProcDebug::PrintReference( ref ).c_str() );

	for( int i = 0; i < 1 + ref.has_second_component; ++i ) {
		Reference::SingleRef& sref = ref.components[i];
		if( sref.target.type != Reference::BaseRef::BRT_DEFINITION ) {
			continue;
		}
		cassert( sref.indirection_section == S_NONE, "Invalid label (indirection set)" );

		switch( ref.global_section ) {
		case S_CODE:
			msg( E_INFO, E_DEBUG, "Definition of TEXT label: assigning address %zu",
			     limits.Code() );
			sref.target.memory_address = limits.Code();
			break;

		case S_DATA:
			msg( E_INFO, E_DEBUG, "Definition of DATA label: assigning address %zu",
			     limits.Data() );
			sref.target.memory_address = limits.Data();
			break;

		case S_BYTEPOOL:
			msg( E_INFO, E_DEBUG, "Definition of BYTEPOOL label: assigning address %zu",
			     limits.Bytepool() );
			sref.target.memory_address = limits.Bytepool();

			break;

		case S_REGISTER:
		case S_FRAME:
		case S_FRAME_BACK:
			casshole( "Definition of label: type %s, cannot auto-assign",
			          ProcDebug::Print( ref.global_section ).c_str() );

		default:
		case S_NONE:
		case S_MAX:
			casshole( "Invalid symbol type" );
			break;
		}

		sref.target.type = Reference::BaseRef::BRT_MEMORY_REF;
	} // needs linker placement
}

void UATLinker::DirectLink_Add( symbol_map&& symbols, const Offsets& limits )
{
	verify_method;
	msg( E_INFO, E_DEBUG, "(Direct link) adding symbols: %zu", symbols.size() );

	char sym_nm_buf[STATIC_LENGTH];

	for( symbol_map::value_type & symbol_record: symbols ) {
		Symbol& symbol			= symbol_record.second.second;
		const std::string& name	= symbol_record.second.first;
		size_t hash				= symbol.hash;

		snprintf( sym_nm_buf, STATIC_LENGTH, "\"%s\" (hash %zx)", name.c_str(), hash );

		cassert( symbol_record.first == symbol.hash,
		         "Input symbol inconsistence (%s): hash %zx <-> key %zx",
		         sym_nm_buf, symbol.hash, symbol_record.first );

		// If symbol is defined here, link it (set address).
		if( symbol.is_resolved ) {
			DirectLink_HandleReference( symbol.ref, limits );
			msg( E_INFO, E_DEBUG, "Definition of symbol %s", sym_nm_buf );
		} // if symbol is resolved (defined)

		else {
			msg( E_INFO, E_DEBUG, "Usage of symbol %s", sym_nm_buf );
		}

		temporary_map.insert( symbol_record );
	}

	msg( E_INFO, E_DEBUG, "(Direct link) add completed" );
}

DirectReference UATLinker::Resolve( const Reference& reference, bool* partial_resolution )
{
	verify_method;

	if( partial_resolution ) {
		msg( E_INFO, E_DEBUG, "Partially resolving reference to %s",
		     ProcDebug::PrintReference( reference, proc_->MMU() ).c_str() );
	} else {
		msg( E_INFO, E_DEBUG, "Resolving reference to %s",
		     ProcDebug::PrintReference( reference, proc_->MMU() ).c_str() );
	}

	DirectReference result; mem_init( result );
	result.section = reference.global_section;
	msg( E_INFO, E_DEBUG, "Global section: %s", ProcDebug::Print( result.section ).c_str() );

	for( unsigned i = 0; i <= reference.has_second_component; ++i ) {
		DirectReference tmp_reference; mem_init( tmp_reference );

		const Reference::SingleRef& cref = reference.components[i];
		const Reference::BaseRef& bref = cref.target;
		msg( E_INFO, E_DEBUG, "[Component %u]: %s", i, cref.indirection_section == S_NONE ? "direct" : "indirect" );

		cassert( bref.type != Reference::BaseRef::BRT_DEFINITION, "[Component %u]: unplaced - resolution is not possible", i );

		/* resolve main base address of the component */
		if( bref.type == Reference::BaseRef::BRT_SYMBOL ) {
			msg( E_INFO, E_DEBUG, "[Component %u]: reference to symbol, hash %zx", i, bref.symbol_hash );
			symbol_type& referenced_symbol = proc_->MMU()->ASymbol( bref.symbol_hash );
			cverify( referenced_symbol.second.is_resolved, "Undefined symbol requested at runtime: \"%s\"",
					 referenced_symbol.first.c_str() );
			tmp_reference = Resolve( referenced_symbol.second.ref, partial_resolution );
		}

		else {
			msg( E_INFO, E_DEBUG, "[Component %u]: memory offset %zu", i, bref.memory_address );
			tmp_reference.address = bref.memory_address;
		}

		/* resolve indirection */
		if( cref.indirection_section != S_NONE ) {
			/* load explicitly specified section (if it is not specified in symbol) */
			if( tmp_reference.section == S_NONE )
				tmp_reference.section = cref.indirection_section;

			else
				cassert( cref.indirection_section == S_NONE || cref.indirection_section == S_MAX,
						 "Duplicate specified section in indirection" );

			msg( E_INFO, E_DEBUG, "[Component %u]: indirection to section %s",
				 i, ProcDebug::Print( tmp_reference.section ).c_str() );

			/* verify we have section set */
			cassert( tmp_reference.section != S_NONE, "No section specified to resolve indirect address" );

			if( partial_resolution ) {
				/* mark the reference as invalid */
				*partial_resolution = false;
				msg( E_INFO, E_DEBUG, "[Component %u]: partial resolution is active, not resolving indirection", i );
			} else {
				/* load address value */
				proc_->LogicProvider()->Read( tmp_reference ).Get( Value::V_INTEGER, tmp_reference.address );

				msg( E_INFO, E_DEBUG, "[Component %u]: indirection resolved to memory offset %zu",
				     i, tmp_reference.address );
			}

			/* still reset the section (currently tmp_reference.section holds the indirection value section) */
			tmp_reference.section = S_NONE;
		}

		msg( E_INFO, E_DEBUG, "[Component %u]: resolved to %s",
			i, ProcDebug::PrintReference( tmp_reference ).c_str() );

		/* assign to result reference */
		if( tmp_reference.section != S_NONE ) {
			cassert( result.section == S_NONE, "Duplicate specified section" );
			result.section = tmp_reference.section;
		}

		result.address += tmp_reference.address;
	}

	cassert( result.section < S_MAX, "Wrong section in resolved reference" );
	msg( E_INFO, E_DEBUG, "Resolution result: %s", ProcDebug::PrintReference( result ).c_str() );
	return result;
}

void UATLinker::DirectLink_Init()
{
	verify_method;

	msg( E_INFO, E_DEBUG, "Starting link session" );
	temporary_map.clear();

	symbol_map source = proc_->MMU()->DumpSymbolImage();

	msg( E_INFO, E_DEBUG, "Inserting existing symbols (count: %zu)", source.size() );
	for( symbol_map::value_type& source_sym: source ) {
		temporary_map.insert( source_sym );
	}
}

void UATLinker::DirectLink_Commit( bool UAT )
{
	verify_method;

	msg( E_INFO, E_VERBOSE, "Ending link session: linking %lu symbols", temporary_map.size() );
	symbol_map target_map;

	char sym_nm_buf[STATIC_LENGTH];

	for( symbol_map::value_type& symbol_iterator: temporary_map ) {
		symbol_type* current_symbol_record = &symbol_iterator.second;
		size_t hash                        = current_symbol_record->second.hash;
		auto existing_record               = target_map.find( hash );

		snprintf( sym_nm_buf, STATIC_LENGTH, "\"%s\" (hash %zx)",
		          current_symbol_record->first.c_str(), hash );

		cassert( hash == symbol_iterator.first,
		         "Internal map inconsistency in %s: hash %zx <-> key %zx",
		         sym_nm_buf, hash, symbol_iterator.first );

		Symbol& symbol = current_symbol_record->second;

		for( int i = 0; i < 1 + symbol.ref.has_second_component; ++i ) {
			cassert( symbol.ref.components[i].target.type != Reference::BaseRef::BRT_DEFINITION,
					 "Unplaced symbol %s", sym_nm_buf );
		}

		// Replace any reference already present in the map.
		if( existing_record != target_map.end() ) {
			bool existing_is_definition = existing_record->second.second.is_resolved;
			bool new_is_definition = symbol.is_resolved;

			if( existing_is_definition ) {
				cverify( !new_is_definition, "Symbol redefinition: %s", sym_nm_buf );
			} else if( new_is_definition ) {
				existing_record->second.second = symbol;
			}
		} else {
			target_map.insert( symbol_iterator );
		}
	} // for (temporary_map)

	if( UAT ) {
		casshole( "Not implemented" );
	}

	proc_->MMU()->SetSymbolImage( std::move( target_map ) );
	temporary_map.clear(); // Well, MMU should move-assign our map, but who knows...

	msg( E_INFO, E_VERBOSE, "Link session completed" );
}

void UATLinker::RelocateReference( Reference& ref, const Offsets& offsets )
{
	Reference::SingleRef& sref = ref.components[0];
	cassert( sref.indirection_section == S_NONE, "Shall not relocate indirect references" );
	cassert( sref.target.type == Reference::BaseRef::BRT_MEMORY_REF, "Shall not relocate symbol references" );

	MemorySectionIdentifier section( ref.global_section );
	if( !section.isValid() ) {
		msg( E_INFO, E_DEBUG, "Relocating: section %s - not relocating",
		     ProcDebug::Print( ref.global_section ).c_str() );
	} else {
		size_t offset = offsets[section];
		msg( E_INFO, E_DEBUG, "Relocating: section %s offset %zu",
		     ProcDebug::Print( ref.global_section ).c_str(), offset );
		sref.target.memory_address += offset;
	}
}

void UATLinker::Relocate( const Offsets& offsets )
{
	msg( E_INFO, E_DEBUG, "Relocating %zu symbols", temporary_map.size() );

	for( symbol_tmap::value_type& symbol_pair: temporary_map ) {
		Symbol& symbol = symbol_pair.second.second;

		msg( E_INFO, E_DEBUG, "Relocating symbol \"%s\": reference to %s",
			 symbol_pair.second.first.c_str(),
			 ProcDebug::PrintReference( symbol.ref, nullptr ).c_str() );

		// Relocate only defined symbol records.
		// Reason: for relocation of an image A (i. e., shifting all data in the image A)
		// we need to adjust only symbols pointing to definitions inside the image A -
		// that is, only defined symbols.
		// It is guaranteed that any unresolved/undefined symbol in the map
		// cannot be resolved inside the given context (and so points to some definition that
		// is going to be imported in process of merging).
		if( !symbol.is_resolved ) {
			continue;
		}

		Reference& ref = symbol.ref;

		// Do not relocate what seems to be an alias.
		// Reason: too much corner-cases.
		// Though we can relocate bicomponent plain references, despite they seem highly
		// unusual (like "d:(4+2)").
		bool do_skip = false;
		for( int i = 0; i < 1 + ref.has_second_component; ++i ) {
			cassert( ref.components[i].target.type != Reference::BaseRef::BRT_DEFINITION,
					 "Reference needs to be link-placed; inconsistency." );

			if( ref.components[i].indirection_section != S_NONE ||
				ref.components[i].target.type != Reference::BaseRef::BRT_MEMORY_REF ) {
				do_skip = true;
				break;
			}
		}

		if( !do_skip ) {
			RelocateReference( ref, offsets );
		}
	}
}

void UATLinker::MergeLink_Add( symbol_map&& symbols )
{
	/*
	 * The semantics of merge-linking.
	 *
	 * If the context to be merged is going to be pasted at offset == 0
	 * (precisely like it is done in the API context merge implementation),
	 * then we just need to merge the symbol maps.
	 */

	msg( E_INFO, E_DEBUG, "Merge-linking %zu symbols", symbols.size() );

	for( const symbol_map::value_type& target_sym: symbols ) {
		temporary_map.insert( target_sym );
	}
}

} // namespace ProcessorImplementation
// kate: indent-mode cstyle; indent-width 4; replace-tabs off; tab-width 4;

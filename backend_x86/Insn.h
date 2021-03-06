#ifndef INTERPRETER_X86BACKEND_INSN_H
#define INTERPRETER_X86BACKEND_INSN_H

#include "build.h"

#include "Interfaces.h"
#include "Defs.h"
#include "Registers.h"
#include "REX.h"
#include "ModRM.h"
#include "SIB.h"

// -------------------------------------------------------------------------------------
// Library:		Homework
// File:		Insn.h
// Author:		Ivan Shapovalov <intelfx100@gmail.com>
// Description:	x86 JIT backend: instruction emitter class definition.
// -------------------------------------------------------------------------------------

namespace x86backend
{

class Insn
{
	static const size_t PREFIXES_COUNT = 4;

	unsigned char prefix_[PREFIXES_COUNT];

	REX rex_;
	ModRM modrm_;
	SIB sib_;

	std::vector<OperandType> operands_;

	llarray immediates_;
	DisplacementHelper displacement_;
	llarray opcode_bytes_;

	// Opcode generation parameters.
	struct {
		AddressSize operand_size;
		bool is_default_64bit_opsize : 1;
		bool unconditionally_need_rex : 1;
		bool need_opcode_reg_extension : 1;
		bool need_modrm_reg_extension : 1;
		bool need_modrm_rm_extension : 1;
		bool need_sib_base_extension : 1;
		bool need_sib_index_extension : 1;
		bool used_modrm_reg : 1;
		bool used_modrm_rm : 1;
		bool used_opcode_reg : 1;
		bool used_sib : 1;
	} flags_;

	// Set prefixes according to flags.
	// Prefixes modified: REX, operand size override.
	void GeneratePrefixes() {
		switch( flags_.operand_size ) {
		case AddressSize::BYTE:
			/* shall be handled in the opcode itself; no-op */
			break;
		case AddressSize::WORD:
			prefix_[2] = static_cast<unsigned char>( Prefixes::SizeOverride::Operand );
			break;
		case AddressSize::DWORD:
			/* no-op */
			break;
		case AddressSize::QWORD:
			if( !flags_.is_default_64bit_opsize ) {
				rex_.w = true;
			}
			break;

		case AddressSize::NONE:
			/* invariant no-op */
			break;

		default:
			s_casshole( "Switch error" );
		}

		if( flags_.need_opcode_reg_extension ) {
			s_cassert( !rex_.b, "Cannot set opcode reg field extension: REX.B field already used" );
			rex_.b = true;
		}
		if( flags_.need_modrm_reg_extension ) {
			s_cassert( !rex_.r, "Cannot set ModR/M reg field extension: REX.R field already used" );
			rex_.r = true;
		}
		if( flags_.need_modrm_rm_extension ) {
			s_cassert( !rex_.b, "Cannot set ModR/M r/m field extension: REX.B field already used" );
			rex_.b = true;
		}
		if( flags_.need_sib_base_extension ) {
			s_cassert( !rex_.b, "Cannot set SIB base field extension: REX.B field already used" );
			rex_.b = true;
		}
		if( flags_.need_sib_index_extension ) {
			s_cassert( !rex_.x, "Cannot set SIB index field extension: REX.X field already used" );
			rex_.x = true;
		}

		if( flags_.unconditionally_need_rex || rex_.IsSet() ) {
			rex_.Enable();
		}
	}

	void AddOperand( AddressSize size, OperandType type)
	{
		if( flags_.operand_size == AddressSize::NONE ) {
			flags_.operand_size = size;
		}
		operands_.push_back( type );
	}

public:

	Insn()
	{
		reinterpret_cast<uint32_t&>( prefix_ ) = 0;
		reinterpret_cast<unsigned char&>( rex_ ) = 0;
		reinterpret_cast<unsigned char&>( modrm_ ) = 0;
 		reinterpret_cast<unsigned char&>( sib_ ) = 0;
		memset( &flags_, 0, sizeof( flags_ ) );
	}

	Insn& SetIsDefault64Bit( bool arg = true )
	{
		flags_.is_default_64bit_opsize = arg;
		return *this;
	}

	Insn& SetNeedREX( bool arg = true )
	{
		flags_.unconditionally_need_rex = arg;
		return *this;
	}

	Insn& SetPrefix( Prefixes::GeneralPurpose p )
	{
		prefix_[0] = static_cast<unsigned char>( p );
		return *this;
	}

	Insn& SetPrefix( Prefixes::SegmentOverride p )
	{
		prefix_[1] = static_cast<unsigned char>( p );
		return *this;
	}

	Insn& SetPrefix( Prefixes::Special p )
	{
		prefix_[2] = static_cast<unsigned char>( p );
		return *this;
	}

	Insn& SetOperandSize( AddressSize s = AddressSize::DWORD /* the default one */ )
	{
		flags_.operand_size = s;
		return *this;
	}

	Insn& AddOpcode( unsigned char opcode )
	{
		opcode_bytes_.append( 1, &opcode );
		return *this;
	}

	template <typename... Args>
	Insn& AddOpcode( unsigned char opcode, Args... args )
	{
		AddOpcode( opcode );
		AddOpcode( args... );
		return *this;
	}

	Insn& SetOpcodeExtension( unsigned char opcode_ext )
	{
		s_cassert( !flags_.used_modrm_reg, "Cannot set opcode extension: reg field already taken" );
		modrm_.reg = 0x7 & opcode_ext;

		flags_.used_modrm_reg = true;
		return *this;
	}

	Insn& AddRegister( RegisterWrapper reg )
	{
		if( flags_.used_modrm_reg && !flags_.used_modrm_rm ) {
			return AddRM( reg );
		}

		s_cassert( !flags_.used_modrm_reg, "Cannot set register: reg field already taken" );
		modrm_.reg = 0x7 & reg.raw;

		flags_.used_modrm_reg = true;
		flags_.need_modrm_reg_extension = reg.need_extension;
		AddOperand( reg.operand_size, OperandType::Register );
		return *this;
	}

	Insn& AddOpcodeRegister( RegisterWrapper reg )
	{
		s_cassert( !opcode_bytes_.empty(), "Cannot set register in opcode: no opcode set" );
		s_cassert( !flags_.used_opcode_reg, "Cannot set register in opcode: lower bits of opcode already taken" );
		char& opcode_ = opcode_bytes_.back();

		s_cassert( !( 0x7 & opcode_ ), "Cannot set register in opcode: lower bits of opcode are non-zero" );
		opcode_ |= ( 0x7 & reg.raw );

		flags_.used_opcode_reg = true;
		flags_.need_opcode_reg_extension = reg.need_extension;
		AddOperand( reg.operand_size, OperandType::OpcodeRegister );
		return *this;
	}

	Insn& AddRM( ModRMWrapper rm )
	{
		s_cassert( !modrm_.rm, "Cannot set r/m: r/m field already taken" );
		s_cassert( !modrm_.mod, "Cannot set r/m: mod field already taken" );
		modrm_.rm = 0x7 & rm.raw_rm;
		modrm_.mod = rm.mod;

		flags_.used_modrm_rm = true;
		flags_.need_modrm_rm_extension = rm.need_extension;

		if( rm.sib.valid ) {
			sib_.base = 0x7 & rm.sib.base_raw;
			sib_.index = 0x7 & rm.sib.index_raw;
			sib_.scale = 0x3 & rm.sib.scale;

			flags_.used_sib = true;
			flags_.need_sib_base_extension = rm.sib.need_base_extension;
			flags_.need_sib_index_extension = rm.sib.need_index_extension;
		}

		AddOperand( rm.operand_size, OperandType::RegMem );

		displacement_ = rm.displacement;
		if( displacement_.size != AddressSize::NONE ) {
			AddOperand( displacement_.size, OperandType::Immediate );
		}

		return *this;
	}

	template <typename T>
	Insn& AddImmediate( const T& imm )
	{
		size_t imm_size = sizeof( T );
		immediates_.append( imm_size, &imm );
		AddOperand( EncodeSize( imm_size ), OperandType::Immediate );
		return *this;
	}

	Insn& AddDisplacement( DisplacementHelper disp )
	{
		displacement_ = disp;
		if( displacement_.size != AddressSize::NONE ) {
			AddOperand( displacement_.size, OperandType::Immediate );
		}
		return *this;
	}

	void Emit( IEmissionDestination* dest )
	{
		llarray& ret = dest->Target();

		// Prefixes
		GeneratePrefixes();
		for( size_t i = 0; i < PREFIXES_COUNT; ++i ) {
			if( prefix_[i] ) {
				ret.append( 1, &prefix_[i] );
			}
		}
		if( rex_.Enabled() ) {
			ret.append( 1, &rex_ );
		}

		// Opcode
		ret.append( opcode_bytes_ );

		// ModR/M and displacement
		if( flags_.used_modrm_reg || flags_.used_modrm_rm ) {
			ret.append( 1, &modrm_ );

			if( modrm_.UsingSIB() ) {
				s_cassert( flags_.used_sib, "SIB unset when required by ModR/M" );
				ret.append( 1, &sib_ );
			}

			switch( modrm_.UsingDisplacement() ) {
			case ModField::Direct:
			case ModField::NoShift:
				s_cassert( displacement_.status == DisplacementStatus::DISPLACEMENT_UNSET,
					       "Displacement set when not required by ModR/M" );
				break;

			case ModField::Disp8:
				s_cassert( displacement_.status == DisplacementStatus::DISPLACEMENT_SET,
						   "Displacement unset or to instruction when disp8 is required by ModR/M" );
				break;

			case ModField::Disp32:
				s_cassert( displacement_.status != DisplacementStatus::DISPLACEMENT_UNSET,
					"Displacement unset when disp32 is required by ModR/M" );
				break;

			default:
				s_casshole( "Switch error" );
			}
		}

		// Displacement
		switch( displacement_.status ) {
		case DisplacementStatus::DISPLACEMENT_UNSET:
			/* no-op */
			break;

		case DisplacementStatus::DISPLACEMENT_SET:
			if( displacement_.size == AddressSize::BYTE ) {
				ret.append( 1, &displacement_.disp8 );
			} else if( displacement_.size == AddressSize::DWORD ) {
				ret.append( 4, &displacement_.disp32 );
			} else {
				s_casshole( "Displacement has wrong size" );
			}
			break;

		case DisplacementStatus::DISPLACEMENT_TO_INSN:
			s_cassert( displacement_.size == AddressSize::DWORD, "Displacement to instruction has wrong size" );
			dest->AddCodeReference( displacement_.insn, true );
			break;

		default:
			s_casshole( "Switch error" );
		}

		// Immediates
		ret.append( immediates_ );
	}
};

} // namespace x86backend

#endif // INTERPRETER_X86BACKEND_INSN_H
// kate: indent-mode cstyle; indent-width 4; replace-tabs off; tab-width 4;

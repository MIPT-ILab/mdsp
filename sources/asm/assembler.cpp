/** @file assembler.cpp
 * Implementation of Assembler class methods
 * @author Alexander Potashev
 * Copyright 2009 MDSP team
 */

#include "assembler.h"
#include "operation.h"


Assembler::Assembler( std::vector<SemanticUnit *> units)
{
    this->units = units;
}

Assembler::~Assembler()
{
}

std::map<unsigned int, hostUInt8> Assembler::run()
{
    /* This variable is used to keep track of the current address in
     * program memory while following the sequence of semantic units */
    unsigned int pc = 0; // which type is better to use here?

    /* map from semantic units in the source code to the current
     * addresses in program memory */
    std::map<SemanticUnit *, unsigned int> unit_addr;

    /* map from label identifiers to its absolute byte addresses
     * in program memory */
    std::map<std::string, unsigned int> label_addr;

    for ( int i = 0; i < (int)units.size(); i ++)
    {
        unit_addr[units[i]] = pc;

        if ( units[i]->type() == UNIT_LABEL)
        {
            label_addr[units[i]->str()] = pc;
        }
        else if ( units[i]->type() == UNIT_OPERATION)
        {
            pc ++; // add operation length in command memory words
        }
    }

    /* map from program memory byte addresses to encoded instructions
     * starting at those addresses */
    std::map<unsigned int, ByteLine *> op_list;
    for ( int i = 0; i < (int)units.size(); i ++)
    {
        if ( units[i]->type() == UNIT_OPERATION)
        {
            pc = unit_addr[units[i]];
            if ( op_list.find( pc) != op_list.end())
            {
                assert(0);
                throw; // 2 operations at the same address
            }

            op_list[pc] = encodeOperation( units[i], pc);
        }
    }

    /* TODO: use class MemoryModel (when it will come usable) */
    /* map from byte addresses in program memory to the values
     * of those bytes */
    std::map<unsigned int, hostUInt8> addr_byte;

    for ( std::map<unsigned int, ByteLine *>::iterator op = op_list.begin();
          op != op_list.end(); op ++)
    {
        unsigned int start_addr = op->first * 4; // in bytes
        ByteLine *bytes = op->second;

        for ( int i = 0; i < (int)bytes->getSizeOfLine(); i ++)
        {
            if ( addr_byte.find( start_addr + i) != addr_byte.end())
            {
                assert(0);
                throw; // there already is a byte at this address
            }

            addr_byte[start_addr + i] = bytes->getByteVal( i);
        }
    }

    return addr_byte;
}

ByteLine *Assembler::encodeOperation(
    SemanticUnit *operation, unsigned int pc)
{
    /* This method is only applicable to assembler commands */
    assert( operation->type() == UNIT_OPERATION);

    Operation op;

    if ( *operation == "brm")
    {
        assert( operation->nOperands() == 2);

        hostUInt8 sd = 0;
        if ( (*operation)[0]->isIndirectGpr() &&
             (*operation)[1]->isDirectGpr())
        {
            sd = 1; // m(reg) -> reg
        }
        else if ( (*operation)[0]->isDirectGpr() &&
                  (*operation)[1]->isIndirectGpr())
        {
            sd = 0; // reg -> m(reg)
        }
        else
        {
            assert(0);
            throw; // invalid combination of opcode and operands
        }

        op.setMOVE( BRM, sd, 0,
                getGprNum( (*operation)[0]->str()),
                getGprNum( (*operation)[1]->str()));
    }
    else if ( *operation == "brr")
    {
        assert( operation->nOperands() == 2);
        assert( (*operation)[0]->isDirectGpr() &&
                (*operation)[1]->isDirectGpr());

        op.setMOVE( BRR, 0, 0,
                getGprNum( (*operation)[0]->str()),
                getGprNum( (*operation)[1]->str()));
    }
    else if ( *operation == "ld")
    {
        assert( operation->nOperands() == 2);
        assert( (*operation)[0]->isConstInt());

        hostUInt8 sd = 0;
        if ( (*operation)[1]->isDirectGpr())
        {
            sd = 0; // <const> -> reg
        }
        else if ( (*operation)[1]->isIndirectGpr())
        {
            sd = 1; // <const> -> m(reg)
        }
        else
        {
            assert(0);
            throw; // invalid combination of opcode and operands
        }

        /* LD command always has an imm16 operand */
        int imm = (*operation)[0]->integer();
        assert( imm >= 0 && imm <= 0xffff);

        hostUInt16 imm16 = (hostUInt16)imm;

        op.setMOVE( LD, sd, imm16, 0,
                getGprNum( (*operation)[1]->str()));
    }
    else if ( *operation == "add")
    {
        hostUInt8 am = 0;
        hostUInt8 rs1 = 0;
        hostUInt8 rs2 = 0;
        hostUInt8 rd = 0;
        hostUInt16 imm10 = 0;

        if ( operation->nOperands() == 3)
        {
            assert( (*operation)[0]->isDirectOrIndirectGpr());
            assert( (*operation)[1]->isDirectOrIndirectGpr());
            assert( (*operation)[2]->isDirectOrIndirectGpr());

            rs1 = getGprNum( (*operation)[0]->str());
            rs2 = getGprNum( (*operation)[1]->str());
            rd = getGprNum( (*operation)[2]->str());

            if ( (*operation)[0]->isDirectGpr() &&
                 (*operation)[1]->isDirectGpr() &&
                 (*operation)[2]->isDirectGpr())
            {
                am = 0; // register direct (all operands are registers)
            }
            else if ( (*operation)[0]->isIndirectGpr() &&
                      (*operation)[1]->isIndirectGpr() &&
                      (*operation)[2]->isIndirectGpr())
            {
                am = 2; // register indirect (all operands are in memory)
            }
            else
            {
                assert(0);
                throw; // wrong operand types for 'add'
            }
        }
        else
        {
            assert(0);
            throw; // wrong numbers of operands for 'add'
        }

        op.setALU( NOP, ADD, NOP, am, imm10, rs1, rs2, rd);
    }
    else if ( *operation == "nop")
    {
        if ( operation->nOperands() != 0)
        {
            assert(0);
            throw;
        }

        op.setALU( NOP, NOP, NOP, 0, 0, 0, 0, 0);
    }
    else
    {
        cout << "Unknown operation: " << operation->str() << endl;
        throw;
    }

    return op.encode();
}

int Assembler::getGprNum( std::string id)
{
    assert( id[0] == '%' && id[1] == 'r');
    assert( isdigit( id[2]));

    if ( id.size() == 3)
    {
        return (int)(id[2] - '0');
    }
    else if ( id.size() == 4)
    {
        assert( isdigit( id[3]));

        int x = 10 * (int)(id[2] - '0') + (int)(id[3] - '0');
        assert( x <= 31);

        return x;
    }
    else
    {
        throw;
    }
}


#pragma once

#include "Game.h"
#include "Grid.h"
#include "Bitboard.h"

constexpr int pieceSize = 80;
constexpr int WHITE = 1;
constexpr int BLACK = -1;

enum AllBitBoards
{
    WhitePawns,
    WhiteKnights,
    WhiteBishops,
    WhiteRooks,
    WhiteQueens,
    WhiteKing,
    BlackPawns,
    BlackKnights,
    BlackBishops,
    BlackRooks,
    BlackQueens,
    BlackKing,
    WhitePieces,
    BlackPieces,
    OccupiedSquares,
    EmptySquares,
    NumBitBoards
};

class Chess : public Game
{
public:
    Chess();
    ~Chess();

    void setUpBoard() override;

    bool canBitMoveFrom(Bit &bit, BitHolder &src) override;
    bool canBitMoveFromTo(Bit &bit, BitHolder &src, BitHolder &dst) override;
    bool actionForEmptyHolder(BitHolder &holder) override;
    void bitMovedFromTo(Bit &bit, BitHolder &src, BitHolder &dst) override;

    void stopGame() override;

    Player *checkForWinner() override;
    bool checkForDraw() override;

    std::string initialStateString() override;
    std::string stateString() override;
    void setStateString(const std::string &s) override;
    void clearBoardHighlights() override;

    Grid* getGrid() override { return _grid; }

private:
    char stateNotation(const char *state, int row, int col) {return state[row * 8 + col]; }
    Bit* PieceForPlayer(const int playerNumber, ChessPiece piece);
    Player* ownerAt(int x, int y) const;
    void FENtoBoard(const std::string& fen);
    char pieceNotation(int x, int y) const;
    BitBoard generateKnightMovesBitboard(int square);
    void generateKnightMoves(std::vector<BitMove>& moves, BitBoard knightBoard, uint64_t occupancy);
    void generateKingMoves(std::vector<BitMove>& moves, BitBoard kingBoard, uint64_t occupancy);
    void generateRookMoves(std::vector<BitMove>& moves, BitBoard rookBoard, uint64_t occupancy);
    void generateBishopMoves(std::vector<BitMove>& moves, BitBoard bishopBoard, uint64_t occupancy);
    void generateQueenMoves(std::vector<BitMove>& moves, BitBoard queenBoard, uint64_t occupancy);

    void generatePawnMoveList(std::vector<BitMove>& moves, const BitBoard pawns, const BitBoard emptySquares, int colorAsInt);

    std::vector<BitMove> generateAllMoves();
    bool isSquareAttacked(int square, int attackerColor, const BitBoard boards[NumBitBoards]) const;
    bool moveLeavesKingInCheck(const BitMove& move, int movingColor) const;
    void rebuildAggregateBoards(BitBoard boards[NumBitBoards]) const;
    int getPieceBoardIndex(int color, int pieceType) const;
    void addPawnBitboardMovesToList(std::vector<BitMove>& moves, const BitBoard pawnBoard, const int shift);
    void addMoveIfValid(const char *state, std::vector<BitMove>& moves, int fromRow, int fromCol, int toRow, int toCol);
    void generatePawnMoves(const char *state, std::vector<BitMove>& moves, int row, int col, int colorAsInt);
    void generateKnightMoves(std::vector<BitMove>& moves, std::string &state);

    inline int bitScanForward(uint64_t bb) const {
#if defined(_MSC_VER) && !defined(__clang__)
        unsigned long index;
        _BitScanForward64(&index, bb);
        return index;
#else
        return __builtin_ffsll(bb) - 1;
#endif
    };

    int _currentPlayer;
    Grid* _grid;
    BitBoard _knightBitboards[64];
    std::vector<BitMove> _moves;
    BitBoard _bitBoards[NumBitBoards];
    int _bitboardLookup[128];
};
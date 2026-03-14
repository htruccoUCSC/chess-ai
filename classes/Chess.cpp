#include "Chess.h"
#include "MagicBitboards.h"
#include "Evaluate.h"
#include <limits>
#include <cmath>
#include <cctype>

namespace {
int g_magicBitboardsRefCount = 0;
constexpr int kMateScore = 100000;
constexpr int kDefaultAIDepth = 4;
}

void printBitboard(uint64_t bitboard) {
    std::cout << "\n  a b c d e f g h\n";
    for (int rank = 7; rank >= 0; rank--) {
        std::cout << (rank + 1) << " ";
        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;
            if (bitboard & (1ULL << square)) {
                std::cout << "X ";
            } else {
                std::cout << ". ";
            }
        }
        std::cout << (rank + 1) << "\n";
        std::cout << std::flush;
    }
    std::cout << "  a b c d e f g h\n";
    std::cout << std::flush;
}

Chess::Chess() {
    if (g_magicBitboardsRefCount++ == 0) {
        initMagicBitboards();
    }

    _grid = new Grid(8, 8);
    for (int i = 0; i < 64; i++) {
        _knightBitboards[i] = generateKnightMovesBitboard(i);
    }

    for (int i = 0; i < 128; i++) {
        _bitboardLookup[i] = 0;
    }

    _bitboardLookup['P'] = WhitePawns;
    _bitboardLookup['N'] = WhiteKnights;
    _bitboardLookup['B'] = WhiteBishops;
    _bitboardLookup['R'] = WhiteRooks;
    _bitboardLookup['Q'] = WhiteQueens;
    _bitboardLookup['K'] = WhiteKing;
    _bitboardLookup['p'] = BlackPawns;
    _bitboardLookup['n'] = BlackKnights;
    _bitboardLookup['b'] = BlackBishops;
    _bitboardLookup['r'] = BlackRooks;
    _bitboardLookup['q'] = BlackQueens;
    _bitboardLookup['k'] = BlackKing;
    _bitboardLookup['0'] = EmptySquares; 
}

Chess::~Chess()
{
    delete _grid;

    if (--g_magicBitboardsRefCount == 0) {
        cleanupMagicBitboards();
    }
}

int Chess::getPieceBoardIndex(int color, int pieceType) const
{
    if (color == WHITE) {
        switch (pieceType) {
            case Pawn: return WhitePawns;
            case Knight: return WhiteKnights;
            case Bishop: return WhiteBishops;
            case Rook: return WhiteRooks;
            case Queen: return WhiteQueens;
            case King: return WhiteKing;
            default: return -1;
        }
    }

    switch (pieceType) {
        case Pawn: return BlackPawns;
        case Knight: return BlackKnights;
        case Bishop: return BlackBishops;
        case Rook: return BlackRooks;
        case Queen: return BlackQueens;
        case King: return BlackKing;
        default: return -1;
    }
}

void Chess::rebuildAggregateBoards(BitBoard boards[NumBitBoards]) const
{
    boards[WhitePieces] = boards[WhitePawns].getData() |
                         boards[WhiteKnights].getData() |
                         boards[WhiteBishops].getData() |
                         boards[WhiteRooks].getData() |
                         boards[WhiteQueens].getData() |
                         boards[WhiteKing].getData();

    boards[BlackPieces] = boards[BlackPawns].getData() |
                         boards[BlackKnights].getData() |
                         boards[BlackBishops].getData() |
                         boards[BlackRooks].getData() |
                         boards[BlackQueens].getData() |
                         boards[BlackKing].getData();

    boards[OccupiedSquares] = boards[WhitePieces].getData() | boards[BlackPieces].getData();
    boards[EmptySquares] = ~boards[OccupiedSquares].getData();
}

char Chess::pieceNotation(int x, int y) const
{
    const char *wpieces = { "0PNBRQK" };
    const char *bpieces = { "0pnbrqk" };
    Bit *bit = _grid->getSquare(x, y)->bit();
    char notation = '0';
    if (bit) {
        notation = bit->gameTag() < 128 ? wpieces[bit->gameTag()] : bpieces[bit->gameTag()-128];
    }
    return notation;
}

Bit* Chess::PieceForPlayer(const int playerNumber, ChessPiece piece)
{
    const char* pieces[] = { "pawn.png", "knight.png", "bishop.png", "rook.png", "queen.png", "king.png" };

    Bit* bit = new Bit();
    // should possibly be cached from player class?
    const char* pieceName = pieces[piece - 1];
    std::string spritePath = std::string("") + (playerNumber == 0 ? "w_" : "b_") + pieceName;
    bit->LoadTextureFromFile(spritePath.c_str());
    bit->setOwner(getPlayerAt(playerNumber));
    bit->setSize(pieceSize, pieceSize);

    return bit;
}

void Chess::setUpBoard()
{
    setNumberOfPlayers(2);
    _gameOptions.rowX = 8;
    _gameOptions.rowY = 8;
    _gameOptions.AIMAXDepth = kDefaultAIDepth;

    _grid->initializeChessSquares(pieceSize, "boardsquare.png");
    FENtoBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR");

    if (gameHasAI()) {
        setAIPlayer(1); // Player 1 is black.
    }

    _currentPlayer = WHITE;
    _moves = generateAllMoves();
    startGame();
}

void Chess::updateAI()
{
    if (!gameHasAI() || _currentPlayer != BLACK) {
        return;
    }

    if (_moves.empty()) {
        _moves = generateAllMoves();
    }
    if (_moves.empty()) {
        return;
    }

    int searchDepth = _gameOptions.AIMAXDepth > 0 ? _gameOptions.AIMAXDepth : kDefaultAIDepth;
    if (searchDepth < 3) {
        searchDepth = 3;
    }

    BitMove bestMove = findBestMove(searchDepth);
    if (bestMove.piece == NoPiece) {
        bestMove = _moves.front();
    }

    executeMoveOnBoard(bestMove);
}

void Chess::FENtoBoard(const std::string& fen) {
    // take only the piece-placement portion this could be useful in the future
    std::string placement = fen.substr(0, fen.find(' '));

    // clear the boar
    _grid->forEachSquare([](ChessSquare* square, int x, int y) {
        square->destroyBit();
    });

    // helper lambda to convert FEN character to ChessPiece enum
    auto fenCharToPiece = [](char c) -> ChessPiece {
        switch (std::tolower(c)) {
            case 'p': return Pawn;
            case 'n': return Knight;
            case 'b': return Bishop;
            case 'r': return Rook;
            case 'q': return Queen;
            case 'k': return King;
            default:  return NoPiece;
        }
    };

    int row = 7;
    int col   = 0;

    // loop through the FEN string and place pieces on the board
    for (char c : placement) {
        // skip slashes and digits
        if (c == '/') {
            row--;
            col = 0;
        } else if (std::isdigit(c)) {
            col += (c - '0');
        } else {
            // get piece type from FEN char
            ChessPiece piece = fenCharToPiece(c);
            // if it's a valid piece, create it and place it on the board
            if (piece != NoPiece) {
                // get board square and create correct piece
                int playerNumber = std::isupper(c) ? 0 : 1;
                Bit* bit = PieceForPlayer(playerNumber, piece);
                ChessSquare* square = _grid->getSquare(col, row);
                bit->setPosition(square->getPosition());
                bit->setGameTag(playerNumber * 128 + piece);
                square->setBit(bit);
            }
            col++;
        }
    }
}

bool Chess::actionForEmptyHolder(BitHolder &holder)
{
    return false;
}

bool Chess::canBitMoveFrom(Bit &bit, BitHolder &src)
{
    // Check if this piece belongs to the current player
    int currentPlayer = getCurrentPlayer()->playerNumber() * 128;
    int pieceColor = bit.gameTag() & 128;
    if (pieceColor != currentPlayer) {
        return false;
    }
    
    // Clear any previous highlights
    clearBoardHighlights();
    
    // Cast to chess square to get position
    ChessSquare* srcSquare = static_cast<ChessSquare*>(&src);
    if (!srcSquare) {
        return true;
    }
    
    int fromSquare = srcSquare->getSquareIndex();
    int pieceType = bit.gameTag() % 128;
    
    // Highlight all valid destination squares for this piece
    for (const auto& move : _moves) {
        if (move.from == fromSquare && move.piece == pieceType) {
            int row = move.to / 8;
            int col = move.to % 8;
            ChessSquare* destSquare = _grid->getSquare(col, row);
            if (destSquare) {
                destSquare->setHighlighted(true);
            }
        }
    }
    
    return true;
}

bool Chess::canBitMoveFromTo(Bit &bit, BitHolder &src, BitHolder &dst)
{
    // Cast holders to chess squares to get their positions
    ChessSquare* srcSquare = static_cast<ChessSquare*>(&src);
    ChessSquare* dstSquare = static_cast<ChessSquare*>(&dst);
    
    if (!srcSquare || !dstSquare) {
        return false;
    }
    
    // Get square indices
    int fromSquare = srcSquare->getSquareIndex();
    int toSquare = dstSquare->getSquareIndex();
    
    // Extract piece type from game tag (gameTag = playerNumber * 128 + piece)
    int pieceType = bit.gameTag() % 128;
    
    // Check if this move exists in our generated moves list
    for (const auto& move : _moves) {
        if (move.from == fromSquare && 
            move.to == toSquare && 
            move.piece == pieceType) {
            return true;
        }
    }
    
    return false;
}

void Chess::clearBoardHighlights()
{
    _grid->forEachSquare([](ChessSquare* square, int x, int y) {
        square->setHighlighted(false);
    });
}

void Chess::bitMovedFromTo(Bit &bit, BitHolder &src, BitHolder &dst)
{
    _currentPlayer = _currentPlayer == WHITE ? BLACK : WHITE; // toggle player
    _moves = generateAllMoves();
    clearBoardHighlights();
    endTurn();
}

void Chess::stopGame()
{
    _grid->forEachSquare([](ChessSquare* square, int x, int y) {
        square->destroyBit();
    });
}

Player* Chess::ownerAt(int x, int y) const
{
    if (x < 0 || x >= 8 || y < 0 || y >= 8) {
        return nullptr;
    }

    auto square = _grid->getSquare(x, y);
    if (!square || !square->bit()) {
        return nullptr;
    }
    return square->bit()->getOwner();
}

Player* Chess::checkForWinner()
{
    return nullptr;
}

bool Chess::checkForDraw()
{
    return false;
}

std::string Chess::initialStateString()
{
    return stateString();
}

std::string Chess::stateString()
{
    std::string s;
    s.reserve(64);
    _grid->forEachSquare([&](ChessSquare* square, int x, int y) {
            s += pieceNotation( x, y );
        }
    );
    return s;}

void Chess::setStateString(const std::string &s)
{
    _grid->forEachSquare([&](ChessSquare* square, int x, int y) {
        int index = y * 8 + x;
        char playerNumber = s[index] - '0';
        if (playerNumber) {
            square->setBit(PieceForPlayer(playerNumber - 1, Pawn));
        } else {
            square->setBit(nullptr);
        }
    });
}

BitBoard Chess::generateKnightMovesBitboard(int square) {
    BitBoard bitboard = 0ULL;
    int rank = square / 8;
    int file = square % 8;

    // Define the possible knight move offsets
    std::pair<int, int> knightOffsets[] = {
        {2, 1}, {2, -1}, {-2, 1}, {-2, -1},
        {1, 2}, {1, -2}, {-1, 2}, {-1, -2}
    };

    constexpr uint64_t oneBit = 1;
    for (auto [dr, df] : knightOffsets) {
        int newRank = rank + dr;
        int newFile = file + df;
        if (newRank >= 0 && newRank < 8 && newFile >= 0 && newFile < 8) {
            bitboard |= (oneBit << (newRank * 8 + newFile));
        }
    }

    return bitboard;
}

void Chess::generateKnightMoves(std::vector<BitMove>& moves, std::string &state)
{
    char knightPiece = _currentPlayer == WHITE ? 'N' : 'n';
    for (char square : state) {
        if (square == knightPiece) {
            // generate moves for this knight
        }
    }
}

void Chess::generateKnightMoves(std::vector<BitMove>& moves, BitBoard knightBoard, uint64_t occupancy) {
    uint64_t friendlyPieces = _currentPlayer == WHITE ? _bitBoards[WhitePieces].getData() : _bitBoards[BlackPieces].getData();
    
    knightBoard.forEachBit([&](int square) {
        BitBoard possibleMoves = _knightBitboards[square];
        uint64_t validMoves = possibleMoves.getData() & ~friendlyPieces;
        
        BitBoard validMovesBoard(validMoves);
        validMovesBoard.forEachBit([&](int targetSquare) {
            moves.push_back(BitMove(square, targetSquare, Knight));
        });
    });
}

void Chess::generateKingMoves(std::vector<BitMove>& moves, BitBoard kingBoard, uint64_t occupancy) {
    uint64_t friendlyPieces = _currentPlayer == WHITE ? _bitBoards[WhitePieces].getData() : _bitBoards[BlackPieces].getData();
    
    kingBoard.forEachBit([&](int square) {
        int rank = square / 8;
        int file = square % 8;
        
        // King can move one square in any direction
        std::pair<int, int> kingOffsets[] = {
            {1, 0}, {1, 1}, {0, 1}, {-1, 1},
            {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
        };
        
        for (auto [dr, df] : kingOffsets) {
            int newRank = rank + dr;
            int newFile = file + df;
            
            if (newRank >= 0 && newRank < 8 && newFile >= 0 && newFile < 8) {
                int targetSquare = newRank * 8 + newFile;
                uint64_t targetBit = 1ULL << targetSquare;
                
                // Can only move to squares not occupied by friendly pieces
                if (!(targetBit & friendlyPieces)) {
                    moves.push_back(BitMove(square, targetSquare, King));
                }
            }
        }
    });
}

void Chess::generateRookMoves(std::vector<BitMove>& moves, BitBoard rookBoard, uint64_t occupancy) {
    uint64_t friendlyPieces = _currentPlayer == WHITE ? _bitBoards[WhitePieces].getData() : _bitBoards[BlackPieces].getData();

    rookBoard.forEachBit([&](int square) {
        uint64_t attacks = getRookAttacks(square, occupancy) & ~friendlyPieces;
        BitBoard attackBoard(attacks);
        attackBoard.forEachBit([&](int targetSquare) {
            moves.push_back(BitMove(square, targetSquare, Rook));
        });
    });
}

void Chess::generateBishopMoves(std::vector<BitMove>& moves, BitBoard bishopBoard, uint64_t occupancy) {
    uint64_t friendlyPieces = _currentPlayer == WHITE ? _bitBoards[WhitePieces].getData() : _bitBoards[BlackPieces].getData();

    bishopBoard.forEachBit([&](int square) {
        uint64_t attacks = getBishopAttacks(square, occupancy) & ~friendlyPieces;
        BitBoard attackBoard(attacks);
        attackBoard.forEachBit([&](int targetSquare) {
            moves.push_back(BitMove(square, targetSquare, Bishop));
        });
    });
}

void Chess::generateQueenMoves(std::vector<BitMove>& moves, BitBoard queenBoard, uint64_t occupancy) {
    uint64_t friendlyPieces = _currentPlayer == WHITE ? _bitBoards[WhitePieces].getData() : _bitBoards[BlackPieces].getData();

    queenBoard.forEachBit([&](int square) {
        uint64_t attacks = getQueenAttacks(square, occupancy) & ~friendlyPieces;
        BitBoard attackBoard(attacks);
        attackBoard.forEachBit([&](int targetSquare) {
            moves.push_back(BitMove(square, targetSquare, Queen));
        });
    });
}

bool Chess::isSquareAttacked(int square, int attackerColor, const BitBoard boards[NumBitBoards]) const
{
    const uint64_t occupied = boards[OccupiedSquares].getData();
    const int rank = square / 8;
    const int file = square % 8;

    const int pawnBoardIndex = (attackerColor == WHITE) ? WhitePawns : BlackPawns;
    const uint64_t pawns = boards[pawnBoardIndex].getData();

    if (attackerColor == WHITE) {
        if (rank > 0) {
            if (file > 0) {
                int source = (rank - 1) * 8 + (file - 1);
                if (pawns & (1ULL << source)) return true;
            }
            if (file < 7) {
                int source = (rank - 1) * 8 + (file + 1);
                if (pawns & (1ULL << source)) return true;
            }
        }
    } else {
        if (rank < 7) {
            if (file > 0) {
                int source = (rank + 1) * 8 + (file - 1);
                if (pawns & (1ULL << source)) return true;
            }
            if (file < 7) {
                int source = (rank + 1) * 8 + (file + 1);
                if (pawns & (1ULL << source)) return true;
            }
        }
    }

    const int knightBoardIndex = (attackerColor == WHITE) ? WhiteKnights : BlackKnights;
    if (_knightBitboards[square].getData() & boards[knightBoardIndex].getData()) {
        return true;
    }

    const int kingBoardIndex = (attackerColor == WHITE) ? WhiteKing : BlackKing;
    if (KingAttacks[square] & boards[kingBoardIndex].getData()) {
        return true;
    }

    const int bishopBoardIndex = (attackerColor == WHITE) ? WhiteBishops : BlackBishops;
    const int rookBoardIndex = (attackerColor == WHITE) ? WhiteRooks : BlackRooks;
    const int queenBoardIndex = (attackerColor == WHITE) ? WhiteQueens : BlackQueens;

    const uint64_t bishopLikeAttackers = boards[bishopBoardIndex].getData() | boards[queenBoardIndex].getData();
    if (getBishopAttacks(square, occupied) & bishopLikeAttackers) {
        return true;
    }

    const uint64_t rookLikeAttackers = boards[rookBoardIndex].getData() | boards[queenBoardIndex].getData();
    if (getRookAttacks(square, occupied) & rookLikeAttackers) {
        return true;
    }

    return false;
}

bool Chess::moveLeavesKingInCheck(const BitMove& move, int movingColor) const
{
    return moveLeavesKingInCheck(move, movingColor, _bitBoards);
}

bool Chess::moveLeavesKingInCheck(const BitMove& move, int movingColor, const BitBoard boards[NumBitBoards]) const
{
    BitBoard boardAfterMove[NumBitBoards];
    for (int i = 0; i < NumBitBoards; ++i) {
        boardAfterMove[i] = boards[i].getData();
    }

    const int movingPieceBoardIndex = getPieceBoardIndex(movingColor, move.piece);
    if (movingPieceBoardIndex < 0) {
        return true;
    }

    const uint64_t fromMask = 1ULL << move.from;
    const uint64_t toMask = 1ULL << move.to;

    boardAfterMove[movingPieceBoardIndex] = (boardAfterMove[movingPieceBoardIndex].getData() & ~fromMask) | toMask;

    if (movingColor == WHITE) {
        boardAfterMove[BlackPawns] = boardAfterMove[BlackPawns].getData() & ~toMask;
        boardAfterMove[BlackKnights] = boardAfterMove[BlackKnights].getData() & ~toMask;
        boardAfterMove[BlackBishops] = boardAfterMove[BlackBishops].getData() & ~toMask;
        boardAfterMove[BlackRooks] = boardAfterMove[BlackRooks].getData() & ~toMask;
        boardAfterMove[BlackQueens] = boardAfterMove[BlackQueens].getData() & ~toMask;
        boardAfterMove[BlackKing] = boardAfterMove[BlackKing].getData() & ~toMask;
    } else {
        boardAfterMove[WhitePawns] = boardAfterMove[WhitePawns].getData() & ~toMask;
        boardAfterMove[WhiteKnights] = boardAfterMove[WhiteKnights].getData() & ~toMask;
        boardAfterMove[WhiteBishops] = boardAfterMove[WhiteBishops].getData() & ~toMask;
        boardAfterMove[WhiteRooks] = boardAfterMove[WhiteRooks].getData() & ~toMask;
        boardAfterMove[WhiteQueens] = boardAfterMove[WhiteQueens].getData() & ~toMask;
        boardAfterMove[WhiteKing] = boardAfterMove[WhiteKing].getData() & ~toMask;
    }

    rebuildAggregateBoards(boardAfterMove);

    const int kingBoardIndex = (movingColor == WHITE) ? WhiteKing : BlackKing;
    const uint64_t kingBoard = boardAfterMove[kingBoardIndex].getData();
    if (kingBoard == 0) {
        return true;
    }

    const int kingSquare = bitScanForward(kingBoard);
    const int enemyColor = (movingColor == WHITE) ? BLACK : WHITE;
    return isSquareAttacked(kingSquare, enemyColor, boardAfterMove);
}

void Chess::buildBoardsFromState(const std::string& state, BitBoard boards[NumBitBoards]) const
{
    for (int i = 0; i < NumBitBoards; ++i) {
        boards[i] = 0;
    }

    for (int i = 0; i < 64; ++i) {
        int bitIndex = _bitboardLookup[static_cast<unsigned char>(state[i])];
        boards[bitIndex] |= (1ULL << i);
        if (state[i] != '0') {
            boards[OccupiedSquares] |= (1ULL << i);
            boards[std::isupper(static_cast<unsigned char>(state[i])) ? WhitePieces : BlackPieces] |= (1ULL << i);
        }
    }

    boards[EmptySquares] = ~boards[OccupiedSquares].getData();
}

std::vector<BitMove> Chess::generateAllMovesForBoard(const BitBoard boards[NumBitBoards], int color) const
{
    std::vector<BitMove> pseudoMoves;
    pseudoMoves.reserve(64);

    const uint64_t occupancy = boards[OccupiedSquares].getData();
    const uint64_t friendlyPieces = color == WHITE ? boards[WhitePieces].getData() : boards[BlackPieces].getData();
    const uint64_t enemyPieces = color == WHITE ? boards[BlackPieces].getData() : boards[WhitePieces].getData();
    const BitBoard pawns = color == WHITE ? boards[WhitePawns] : boards[BlackPawns];
    const BitBoard knights = color == WHITE ? boards[WhiteKnights] : boards[BlackKnights];
    const BitBoard bishops = color == WHITE ? boards[WhiteBishops] : boards[BlackBishops];
    const BitBoard rooks = color == WHITE ? boards[WhiteRooks] : boards[BlackRooks];
    const BitBoard queens = color == WHITE ? boards[WhiteQueens] : boards[BlackQueens];
    const BitBoard king = color == WHITE ? boards[WhiteKing] : boards[BlackKing];

    const int direction = color == WHITE ? 1 : -1;
    const int startRank = color == WHITE ? 1 : 6;
    const uint64_t emptyBits = boards[EmptySquares].getData();
    pawns.forEachBit([&](int square) {
        const int rank = square / 8;
        const int file = square % 8;

        const int forwardSquare = square + (direction * 8);
        if (forwardSquare >= 0 && forwardSquare < 64) {
            const uint64_t forwardBit = 1ULL << forwardSquare;
            if (forwardBit & emptyBits) {
                pseudoMoves.push_back(BitMove(square, forwardSquare, Pawn));
                if (rank == startRank) {
                    const int doubleForwardSquare = square + (direction * 16);
                    if (doubleForwardSquare >= 0 && doubleForwardSquare < 64) {
                        const uint64_t doubleForwardBit = 1ULL << doubleForwardSquare;
                        if (doubleForwardBit & emptyBits) {
                            pseudoMoves.push_back(BitMove(square, doubleForwardSquare, Pawn));
                        }
                    }
                }
            }
        }

        const std::pair<int, int> captureOffsets[] = {{direction, -1}, {direction, 1}};
        for (auto [dr, df] : captureOffsets) {
            const int newRank = rank + dr;
            const int newFile = file + df;
            if (newRank >= 0 && newRank < 8 && newFile >= 0 && newFile < 8) {
                const int targetSquare = newRank * 8 + newFile;
                const uint64_t targetBit = 1ULL << targetSquare;
                if (targetBit & enemyPieces) {
                    pseudoMoves.push_back(BitMove(square, targetSquare, Pawn));
                }
            }
        }
    });

    knights.forEachBit([&](int square) {
        const uint64_t validMoves = _knightBitboards[square].getData() & ~friendlyPieces;
        BitBoard validMovesBoard(validMoves);
        validMovesBoard.forEachBit([&](int targetSquare) {
            pseudoMoves.push_back(BitMove(square, targetSquare, Knight));
        });
    });

    bishops.forEachBit([&](int square) {
        const uint64_t attacks = getBishopAttacks(square, occupancy) & ~friendlyPieces;
        BitBoard attackBoard(attacks);
        attackBoard.forEachBit([&](int targetSquare) {
            pseudoMoves.push_back(BitMove(square, targetSquare, Bishop));
        });
    });

    rooks.forEachBit([&](int square) {
        const uint64_t attacks = getRookAttacks(square, occupancy) & ~friendlyPieces;
        BitBoard attackBoard(attacks);
        attackBoard.forEachBit([&](int targetSquare) {
            pseudoMoves.push_back(BitMove(square, targetSquare, Rook));
        });
    });

    queens.forEachBit([&](int square) {
        const uint64_t attacks = getQueenAttacks(square, occupancy) & ~friendlyPieces;
        BitBoard attackBoard(attacks);
        attackBoard.forEachBit([&](int targetSquare) {
            pseudoMoves.push_back(BitMove(square, targetSquare, Queen));
        });
    });

    king.forEachBit([&](int square) {
        const uint64_t validMoves = KingAttacks[square] & ~friendlyPieces;
        BitBoard validMovesBoard(validMoves);
        validMovesBoard.forEachBit([&](int targetSquare) {
            pseudoMoves.push_back(BitMove(square, targetSquare, King));
        });
    });

    std::vector<BitMove> legalMoves;
    legalMoves.reserve(pseudoMoves.size());
    for (const BitMove& move : pseudoMoves) {
        if (!moveLeavesKingInCheck(move, color, boards)) {
            legalMoves.push_back(move);
        }
    }

    return legalMoves;
}

int Chess::evaluateBoard(const BitBoard boards[NumBitBoards]) const
{
    char state[64];
    for (int i = 0; i < 64; ++i) {
        state[i] = '0';
    }

    boards[WhitePawns].forEachBit([&](int square) { state[square] = 'P'; });
    boards[WhiteKnights].forEachBit([&](int square) { state[square] = 'N'; });
    boards[WhiteBishops].forEachBit([&](int square) { state[square] = 'B'; });
    boards[WhiteRooks].forEachBit([&](int square) { state[square] = 'R'; });
    boards[WhiteQueens].forEachBit([&](int square) { state[square] = 'Q'; });
    boards[WhiteKing].forEachBit([&](int square) { state[square] = 'K'; });
    boards[BlackPawns].forEachBit([&](int square) { state[square] = 'p'; });
    boards[BlackKnights].forEachBit([&](int square) { state[square] = 'n'; });
    boards[BlackBishops].forEachBit([&](int square) { state[square] = 'b'; });
    boards[BlackRooks].forEachBit([&](int square) { state[square] = 'r'; });
    boards[BlackQueens].forEachBit([&](int square) { state[square] = 'q'; });
    boards[BlackKing].forEachBit([&](int square) { state[square] = 'k'; });

    return evaluateBoardFromState(state);
}

int Chess::negamax(BitBoard boards[NumBitBoards], int depth, int alpha, int beta, int colorToMove) const
{
    if (depth <= 0) {
        return colorToMove * evaluateBoard(boards);
    }

    std::vector<BitMove> moves = generateAllMovesForBoard(boards, colorToMove);
    if (moves.empty()) {
        const int kingBoardIndex = colorToMove == WHITE ? WhiteKing : BlackKing;
        const uint64_t kingBoard = boards[kingBoardIndex].getData();
        if (kingBoard == 0) {
            return -kMateScore;
        }
        const int kingSquare = bitScanForward(kingBoard);
        const int attackerColor = colorToMove == WHITE ? BLACK : WHITE;
        if (isSquareAttacked(kingSquare, attackerColor, boards)) {
            return -(kMateScore - depth);
        }
        return 0;
    }

    int bestScore = std::numeric_limits<int>::min() / 2;
    for (const BitMove& move : moves) {
        BitBoard nextBoards[NumBitBoards];
        for (int i = 0; i < NumBitBoards; ++i) {
            nextBoards[i] = boards[i].getData();
        }

        const int movingPieceBoardIndex = getPieceBoardIndex(colorToMove, move.piece);
        if (movingPieceBoardIndex < 0) {
            continue;
        }

        const uint64_t fromMask = 1ULL << move.from;
        const uint64_t toMask = 1ULL << move.to;
        nextBoards[movingPieceBoardIndex] = (nextBoards[movingPieceBoardIndex].getData() & ~fromMask) | toMask;

        if (colorToMove == WHITE) {
            nextBoards[BlackPawns] = nextBoards[BlackPawns].getData() & ~toMask;
            nextBoards[BlackKnights] = nextBoards[BlackKnights].getData() & ~toMask;
            nextBoards[BlackBishops] = nextBoards[BlackBishops].getData() & ~toMask;
            nextBoards[BlackRooks] = nextBoards[BlackRooks].getData() & ~toMask;
            nextBoards[BlackQueens] = nextBoards[BlackQueens].getData() & ~toMask;
            nextBoards[BlackKing] = nextBoards[BlackKing].getData() & ~toMask;
        } else {
            nextBoards[WhitePawns] = nextBoards[WhitePawns].getData() & ~toMask;
            nextBoards[WhiteKnights] = nextBoards[WhiteKnights].getData() & ~toMask;
            nextBoards[WhiteBishops] = nextBoards[WhiteBishops].getData() & ~toMask;
            nextBoards[WhiteRooks] = nextBoards[WhiteRooks].getData() & ~toMask;
            nextBoards[WhiteQueens] = nextBoards[WhiteQueens].getData() & ~toMask;
            nextBoards[WhiteKing] = nextBoards[WhiteKing].getData() & ~toMask;
        }

        rebuildAggregateBoards(nextBoards);

        const int score = -negamax(nextBoards, depth - 1, -beta, -alpha, -colorToMove);
        if (score > bestScore) {
            bestScore = score;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            break;
        }
    }

    return bestScore;
}

BitMove Chess::findBestMove(int depth) const
{
    BitBoard rootBoards[NumBitBoards];
    for (int i = 0; i < NumBitBoards; ++i) {
        rootBoards[i] = _bitBoards[i].getData();
    }

    std::vector<BitMove> rootMoves = generateAllMovesForBoard(rootBoards, _currentPlayer);
    if (rootMoves.empty()) {
        return BitMove();
    }

    int alpha = std::numeric_limits<int>::min() / 2;
    const int beta = std::numeric_limits<int>::max() / 2;
    int bestScore = std::numeric_limits<int>::min() / 2;
    BitMove bestMove = rootMoves.front();

    for (const BitMove& move : rootMoves) {
        BitBoard nextBoards[NumBitBoards];
        for (int i = 0; i < NumBitBoards; ++i) {
            nextBoards[i] = rootBoards[i].getData();
        }

        const int movingPieceBoardIndex = getPieceBoardIndex(_currentPlayer, move.piece);
        if (movingPieceBoardIndex < 0) {
            continue;
        }

        const uint64_t fromMask = 1ULL << move.from;
        const uint64_t toMask = 1ULL << move.to;
        nextBoards[movingPieceBoardIndex] = (nextBoards[movingPieceBoardIndex].getData() & ~fromMask) | toMask;

        if (_currentPlayer == WHITE) {
            nextBoards[BlackPawns] = nextBoards[BlackPawns].getData() & ~toMask;
            nextBoards[BlackKnights] = nextBoards[BlackKnights].getData() & ~toMask;
            nextBoards[BlackBishops] = nextBoards[BlackBishops].getData() & ~toMask;
            nextBoards[BlackRooks] = nextBoards[BlackRooks].getData() & ~toMask;
            nextBoards[BlackQueens] = nextBoards[BlackQueens].getData() & ~toMask;
            nextBoards[BlackKing] = nextBoards[BlackKing].getData() & ~toMask;
        } else {
            nextBoards[WhitePawns] = nextBoards[WhitePawns].getData() & ~toMask;
            nextBoards[WhiteKnights] = nextBoards[WhiteKnights].getData() & ~toMask;
            nextBoards[WhiteBishops] = nextBoards[WhiteBishops].getData() & ~toMask;
            nextBoards[WhiteRooks] = nextBoards[WhiteRooks].getData() & ~toMask;
            nextBoards[WhiteQueens] = nextBoards[WhiteQueens].getData() & ~toMask;
            nextBoards[WhiteKing] = nextBoards[WhiteKing].getData() & ~toMask;
        }

        rebuildAggregateBoards(nextBoards);

        const int score = -negamax(nextBoards, depth - 1, -beta, -alpha, -_currentPlayer);
        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    return bestMove;
}

bool Chess::executeMoveOnBoard(const BitMove& move)
{
    ChessSquare* srcSquare = _grid->getSquare(move.from % 8, move.from / 8);
    ChessSquare* dstSquare = _grid->getSquare(move.to % 8, move.to / 8);
    if (!srcSquare || !dstSquare) {
        return false;
    }

    Bit* movingBit = srcSquare->bit();
    if (!movingBit) {
        return false;
    }

    if (dstSquare->bit()) {
        pieceTaken(dstSquare->bit());
    }

    if (!dstSquare->dropBitAtPoint(movingBit, dstSquare->getPosition())) {
        return false;
    }

    srcSquare->draggedBitTo(movingBit, dstSquare);
    movingBit->setPickedUp(false);
    movingBit->setPosition(dstSquare->getPosition());
    bitMovedFromTo(*movingBit, *srcSquare, *dstSquare);
    return true;
}

void Chess::generatePawnMoveList(std::vector<BitMove>& moves, const BitBoard pawns, const BitBoard emptySquares, int colorAsInt) {
    uint64_t enemyPieces = colorAsInt == WHITE ? _bitBoards[BlackPieces].getData() : _bitBoards[WhitePieces].getData();
    uint64_t emptyBits = emptySquares.getData();
    int direction = (colorAsInt == WHITE) ? 1 : -1;  // White pawns move up (+1 rank), black pawns move down (-1 rank)
    int startRank = (colorAsInt == WHITE) ? 1 : 6;   // Starting rank for pawns
    
    pawns.forEachBit([&](int square) {
        int rank = square / 8;
        int file = square % 8;
        
        // Single square forward move
        int forwardSquare = square + (direction * 8);
        if (forwardSquare >= 0 && forwardSquare < 64) {
            uint64_t forwardBit = 1ULL << forwardSquare;
            if (forwardBit & emptyBits) {
                moves.push_back(BitMove(square, forwardSquare, Pawn));
                
                // Double square forward move from starting position
                if (rank == startRank) {
                    int doubleForwardSquare = square + (direction * 16);
                    uint64_t doubleForwardBit = 1ULL << doubleForwardSquare;
                    if (doubleForwardBit & emptyBits) {
                        moves.push_back(BitMove(square, doubleForwardSquare, Pawn));
                    }
                }
            }
        }
        
        // Diagonal captures
        std::pair<int, int> captureOffsets[] = {{direction, -1}, {direction, 1}};
        for (auto [dr, df] : captureOffsets) {
            int newRank = rank + dr;
            int newFile = file + df;
            
            if (newRank >= 0 && newRank < 8 && newFile >= 0 && newFile < 8) {
                int targetSquare = newRank * 8 + newFile;
                uint64_t targetBit = 1ULL << targetSquare;
                
                // Can only capture enemy pieces
                if (targetBit & enemyPieces) {
                    moves.push_back(BitMove(square, targetSquare, Pawn));
                }
            }
        }
    });
}

std::vector<BitMove> Chess::generateAllMoves()
{
    std::vector<BitMove> pseudoMoves;
    pseudoMoves.reserve(64);
    std::string state = stateString();

    for (int i = 0; i < NumBitBoards; i++) {
        _bitBoards[i] = 0;
    }

    for (int i = 0; i < 64; i++) {
        int bitIndex = _bitboardLookup[state[i]];
        _bitBoards[bitIndex] |= (1ULL << i);
        if (state[i] != '0') {
            _bitBoards[OccupiedSquares] |= (1ULL << i);
            _bitBoards[isupper(state[i]) ? WhitePieces : BlackPieces] |= (1ULL << i);
        }
    }

    // Calculate empty squares
    _bitBoards[EmptySquares] = ~_bitBoards[OccupiedSquares].getData();
    const uint64_t occupancy = _bitBoards[OccupiedSquares].getData();
    
    // Generate moves for current player's pieces
    if (_currentPlayer == WHITE) {
        generatePawnMoveList(pseudoMoves, _bitBoards[WhitePawns], _bitBoards[EmptySquares], WHITE);
        generateKnightMoves(pseudoMoves, _bitBoards[WhiteKnights], occupancy);
        generateBishopMoves(pseudoMoves, _bitBoards[WhiteBishops], occupancy);
        generateRookMoves(pseudoMoves, _bitBoards[WhiteRooks], occupancy);
        generateQueenMoves(pseudoMoves, _bitBoards[WhiteQueens], occupancy);
        generateKingMoves(pseudoMoves, _bitBoards[WhiteKing], occupancy);
    } else {
        generatePawnMoveList(pseudoMoves, _bitBoards[BlackPawns], _bitBoards[EmptySquares], BLACK);
        generateKnightMoves(pseudoMoves, _bitBoards[BlackKnights], occupancy);
        generateBishopMoves(pseudoMoves, _bitBoards[BlackBishops], occupancy);
        generateRookMoves(pseudoMoves, _bitBoards[BlackRooks], occupancy);
        generateQueenMoves(pseudoMoves, _bitBoards[BlackQueens], occupancy);
        generateKingMoves(pseudoMoves, _bitBoards[BlackKing], occupancy);
    }

    std::vector<BitMove> legalMoves;
    legalMoves.reserve(pseudoMoves.size());
    const int movingColor = _currentPlayer;
    for (const BitMove& move : pseudoMoves) {
        if (!moveLeavesKingInCheck(move, movingColor)) {
            legalMoves.push_back(move);
        }
    }

    return legalMoves;
}

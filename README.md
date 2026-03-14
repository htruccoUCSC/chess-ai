# Chess AI

## Chess Implementation

Contains chess with the ability for all pieces to move using a bitboard and a magic bitboard. Piece also take correctly and the game does not allow incorrect moves when a king is in check.

Built on macOS.

Starting Board:
![Alt Text](resources/example_board.png)

Move Generator on start:
![Alt Text](resources/move-generator.png)

## Black AI (Negamax + Alpha-Beta)

The chess game now includes an AI that plays as black.

- On black's turn, `updateAI()` runs and searches for the best legal move.
- Move search uses **negamax with alpha-beta pruning**.
- Search depth is configured in `AIMAXDepth`.
- Candidate moves are generated as legal moves only (moves that leave your own king in check are filtered out).
- Board evaluation combines material values with piece-square tables from `classes/Evaluate.h`.

Black looks ahead several plies (what half-turns are called in chess), prunes bad branches early with alpha-beta, scores resulting positions, and plays the highest-scoring legal move.

I left it at a depth of 4 because it worked pretty well and was able to beat me easily. I'm pretty bummed I couldn't get something ready for the competition, but between my other classes and GDC there was no time.

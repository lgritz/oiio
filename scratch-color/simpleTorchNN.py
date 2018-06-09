import numpy as np
import torch
import torch.nn as nn


device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')


class Model(nn.Module):
    def __init__(self):
        super(Model, self).__init__()
        self.fc1 = nn.Linear(2, 1)

    def forward(self, x):
        return self.fc1(x)


# This is non linear but doesn't perform as well for the task
'''
class Model(nn.Module):
    def __init__(self):
        super(Model, self).__init__()
        self.fc1 = nn.Linear(2, 2)
        self.relu1 = nn.ReLU()
        self.fc2 = nn.Linear(2, 1)

    def forward(self, x):
        return self.fc2(self.relu1(self.fc1(x)))
'''


samples = np.random.uniform(0, 1, (10000, 2)).astype(np.float32)
targets = np.average(samples, axis=-1).reshape(10000, 1)
samples = torch.from_numpy(samples).to(device)
targets = torch.from_numpy(targets).to(device)

model = Model().to(device)
optimizer = torch.optim.RMSprop(model.parameters(), lr=0.01)
criterion = nn.MSELoss()
for e in range(7):
    for i in range(10000 // 64):
        loss = criterion(
            model(samples[64 * i: 64 * i + 1]),
            targets[64 * i: 64 * i + 1]
        )

        optimizer.zero_grad()
        loss.backward()
        optimizer.step()

        with torch.no_grad():
            print(
                'Epoch: {:<4} Batch: {:<4} Loss: {:.4f} \t Weights: {} \t Eval-Zero: {:.4f}'.format(
                    e,
                    i,
                    loss.item(),
                    '(fc1) {:.2f} {:.2f}'.format(*model.fc1.weight.cpu().numpy()[0]),
                    model(torch.tensor([[0., 0.]], dtype=torch.float32, device=device)).item()
                )
            )

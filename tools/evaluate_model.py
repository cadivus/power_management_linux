#!/usr/bin/env python

import sys

class EnergyTerm:
    def evaluate(self, frequency):
        pass

class LinearTerm(EnergyTerm):
    def __init__(self, coefficient):
        self.coefficient = coefficient

    def evaluate(self, frequency):
        return self.coefficient * frequency

class SquaredTerm(EnergyTerm):
    def __init__(self, coefficient):
        self.coefficient = coefficient

    def evaluate(self, frequency):
        return self.coefficient * frequency * frequency

class EnergyModel:
    def __init__(self, intercept, terms):
        self.intercept = intercept
        self.terms = terms

    def evaluate(self, frequencies):
        energy = self.intercept
        for term, frequency in zip(self.terms, frequencies):
            energy += term.evaluate(frequency)
        return energy

models = {
    "E": EnergyModel(3.1605309472304457, [
        LinearTerm(3.6757760305158816e-11),
        LinearTerm(3.2089460350838357e-10),
        LinearTerm(1.7825941074649793e-10),
        LinearTerm(2.910827313533828e-06),
        LinearTerm(1.0259849984396058e-09),
    ]),
    "P": EnergyModel(-10.925121778307883, [
        LinearTerm(6.494945101276136e-11),
        LinearTerm(9.039336093376469e-10),
        LinearTerm(1.5063793208625808e-08),
        SquaredTerm(-1.1126473785568833e-19),
        SquaredTerm(-2.785074808986619e-18),
    ]),
}

def read_proc_file(file):
    data = dict()
    for line in file:
        key, value = line.split(':')
        data[key] = value.strip()
    return data

proc_data = read_proc_file(sys.stdin)

counter_names = ["Counter0", "Counter1", "Counter2", "Counter3", "Counter4"]
counters = (float(proc_data[name]) for name in counter_names)

duration = float(proc_data["Duration"])
if duration == 0:
    duration = 1
duration *= 1e-9;

frequencies = (count / duration for count in counters)

model = models[proc_data["CoreType"]]

power = model.evaluate(frequencies)

print("Reported Power: ", proc_data["Power"])
print("Evaluated Power: ", power)
